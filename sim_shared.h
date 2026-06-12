#ifndef ISIMULATOR_H
#define ISIMULATOR_H

#include <vector>
#include <iostream>
#include <utility>
#include <cmath>
#include "config.h"
#include "circle_state.h"

inline float computeHandSmoothingAlpha(float speed, const CircleConfig& cfg) {
    if (speed > cfg.handSpeedThreshold) {
        return cfg.handSmoothingAlphaHigh;
    }
    return cfg.handSmoothingAlphaLow +
        (cfg.handSmoothingAlphaHigh - cfg.handSmoothingAlphaLow) * (speed / cfg.handSpeedThreshold);
}

inline void applyHandSmoothing(
    int rawGridX, int rawGridY,
    float& smoothedX, float& smoothedY,
    int& outX, int& outY,
    bool wasPresent,
    const CircleConfig& cfg,
    float timestep)
{
    if (wasPresent) {
        float dx = static_cast<float>(rawGridX) - smoothedX;
        float dy = static_cast<float>(rawGridY) - smoothedY;
        float speed = std::sqrt(dx * dx + dy * dy) / timestep;
        float adaptiveAlpha = computeHandSmoothingAlpha(speed, cfg);

        smoothedX = adaptiveAlpha * static_cast<float>(rawGridX) + (1.0f - adaptiveAlpha) * smoothedX;
        smoothedY = adaptiveAlpha * static_cast<float>(rawGridY) + (1.0f - adaptiveAlpha) * smoothedY;
        outX = static_cast<int>(smoothedX);
        outY = static_cast<int>(smoothedY);
    } else {
        smoothedX = static_cast<float>(rawGridX);
        smoothedY = static_cast<float>(rawGridY);
        outX = rawGridX;
        outY = rawGridY;
    }
}

inline void applyCircleVelocitySmoothing(
    float instantVelX, float instantVelY,
    float& velX, float& velY,
    float handSpeed,
    const CircleConfig& cfg)
{
    float alpha = computeHandSmoothingAlpha(handSpeed, cfg);
    velX = alpha * instantVelX + (1.0f - alpha) * velX;
    velY = alpha * instantVelY + (1.0f - alpha) * velY;
}

inline bool shouldApplyMomentumTransfer(int deltaX, int deltaY, float deadZone) {
    if (deltaX == 0 && deltaY == 0) return false;
    if (deadZone <= 0.0f) return true;
    float dx = static_cast<float>(deltaX);
    float dy = static_cast<float>(deltaY);
    return dx * dx + dy * dy >= deadZone * deadZone;
}

inline bool shouldApplyMomentumTransferVelocity(float velX, float velY, float deadZone) {
    if (velX == 0.0f && velY == 0.0f) return false;
    if (deadZone <= 0.0f) return true;
    return velX * velX + velY * velY >= deadZone * deadZone;
}

inline int scaleRadiusByZ(float z, int baseRadius, const CircleConfig& cfg) {
    if (z != z) z = 0.0f; // nan check
    float zRange = cfg.zMax - cfg.zMin;
    float t = (zRange > 0.0001f) ? (z - cfg.zMin) / zRange : 0.5f;
    t = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
    float scale = cfg.scaleMin + t * (cfg.scaleMax - cfg.scaleMin);
    return static_cast<int>(baseRadius * scale);
}

struct ImageData {
    void* pixels;
    int width;
    int height;
    int bytesPerPixel;
    int rShift, gShift, bShift;

    ImageData() : pixels(nullptr), width(0), height(0), bytesPerPixel(0), rShift(0), gShift(0), bShift(0) {}
    ImageData(void* p, int w, int h, int bpp, int rS, int gS, int bS)
        : pixels(p), width(w), height(h), bytesPerPixel(bpp), rShift(rS), gShift(gS), bShift(bS) {}
};

class ISimulator {
public:
    virtual ~ISimulator() = default;

    bool inkInitialized = false;

    // sim params
    float gravity;

    // grid params (all in grid coords in sim space)
    int resolution;
    int gridX = 0, gridY = 0;
    float domainHeight = 0.0f, domainWidth = 0.0f;
    float xHeight = 0.0f, yHeight = 0.0f;
    float cellSize = 0.0f;
    float momentumTransferStrength;
    float momentumTransferRadius;

    // Hand tracking circles (mouse mode uses index 0 only)
    CircleState circles[HandTracking::MAX_CIRCLES];
    int baseCircleRadius = 0;
    LineSegment segments[HandTracking::MAX_SEGMENTS];
    int numSegments = 0;

    // Mouse circle state (separate from hand circles array)
    int mouseCircleX = 0, mouseCircleY = 0;
    int mousePrevCircleX = 0, mousePrevCircleY = 0;
    float mouseCircleVelX = 0.0f, mouseCircleVelY = 0.0f;
    int mouseCircleRadius = 0;
    bool isMouseDragging = false;

    // wind tunnel state
    float windTunnelStart; // 0-1 (pass this one in)
    float windTunnelEnd;
    int windTunnelStartCell = 0; // the actual grid coords used in sim; calculated in init
    int windTunnelEndCell = 0;
    int pipeHeight = 0;
    int windTunnelSide; // 0, 1, 2, 3 = left, top, bottom, right; -1 = disabled
    float windTunnelSpeed; // magnitude; direction inferred

    // motion-adaptive solver iterations
    int motionCooldownFrames = 0;
    bool motionDetected = false;
    float effectiveProjectionIters = 200.0f;

    // simulation methods
    virtual bool init(const Config& config, const ImageData* imageData = nullptr, float aspectRatio = 1.5f) =0;
    virtual void update() =0;

    // user interaction helper
    std::pair<int, int> screenToGridCoords(int screenX, int screenY, int windowWidth, int windowHeight) const {
        float simX = screenX / static_cast<float>(windowWidth) * domainWidth;
        float simY = (windowHeight - screenY) / static_cast<float>(windowHeight) * domainHeight;

        int gridX = static_cast<int>(simX / cellSize);
        int gridY = static_cast<int>(simY / cellSize);

        return {gridX, gridY};
    }

    // viewport-aware coordinate mapping for mouse input
    std::pair<int, int> viewportAwareScreenToGrid(int screenX, int screenY, int canvasWidth, int canvasHeight) const {
        // Check which viewport (if any) contains this screen coordinate
        for (int i = 0; i < 4; i++) {
            std::string vpName = "viewport_" + std::to_string(i + 1);
            auto it = g_layoutPixels.components.find(vpName);
            if (it == g_layoutPixels.components.end()) continue;
            const PixelRect& vp = it->second;

            if (screenX >= vp.x && screenX < vp.x + vp.width &&
                screenY >= vp.y && screenY < vp.y + vp.height) {
                // Map screen coords within viewport to sim grid coords (mirrors WGSL fragment shader)
                float normX = (screenX - vp.x) / static_cast<float>(vp.width);
                float normY = (screenY - vp.y) / static_cast<float>(vp.height);
                float simX = normX * domainWidth;
                float simY = (1.0f - normY) * domainHeight;
                int gridXCoord = static_cast<int>(simX / cellSize);
                int gridYCoord = static_cast<int>(simY / cellSize);
                return {gridXCoord, gridYCoord};
            }
        }
        // Not in any viewport -- return invalid coordinates
        return {-1, -1};
    }

    // fields
    virtual const std::vector<float>& getVelocityX() const =0;
    virtual const std::vector<float>& getVelocityY() const =0;
    virtual const std::vector<float>& getPressure() const =0;
    virtual const std::vector<float>& getDensity() const =0;
    virtual const std::vector<float>& getSolid() const =0;
    virtual const std::vector<float>& getRedInk() const =0;
    virtual const std::vector<float>& getGreenInk() const =0;
    virtual const std::vector<float>& getBlueInk() const =0;
    
    // modes
    virtual bool isUsingGPU() const { return false; }

    // Recompute windTunnelStartCell/windTunnelEndCell from config side and normalized span.
    void recomputeWindTunnelCells(const Config& config);

    // Runtime config reload
    virtual void updateSimParams(const Config& config) {}
    virtual void reinitInk(const ImageData* imageData) {}
    virtual void resetFluidState(bool clearInk = true) {}

    // mouse helpers
    bool isInsideMouseCircle(int i, int j) {
        float dx = (i + 0.5f) - mouseCircleX;
        float dy = (j + 0.5f) - mouseCircleY;
        return sqrt(dx * dx + dy * dy) <= mouseCircleRadius;
    }

    void onMouseDown(int mouseX, int mouseY) {
        isMouseDragging = true;
    }

    void onMouseUp() {
        isMouseDragging = false;
        mouseCircleVelX = 0.0f;
        mouseCircleVelY = 0.0f;
        mousePrevCircleX = mouseCircleX;
        mousePrevCircleY = mouseCircleY;
    }

    void onMouseDrag(int mouseX, int mouseY) {
        if (isMouseDragging) {
            // clamp circle to bounds
            int newX = std::max(mouseCircleRadius, std::min(mouseX, gridX - mouseCircleRadius - 1));
            int newY = std::max(mouseCircleRadius, std::min(mouseY, gridY - mouseCircleRadius - 1));
            if (newX != mouseCircleX || newY != mouseCircleY) {
                moveCircle(newX, newY);
            }
        }
    }

    virtual void moveCircle(int newGridX, int newGridY) {}
    // fingertip mode helpers (fuck it, they're always here. I'm too tired for this)
    virtual void updateCircles(const FingertipData* fingertips, int count) {}
    virtual void updateLineSegments(const FingertipData* landmarks, int count) {}
};

#endif