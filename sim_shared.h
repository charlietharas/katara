#ifndef SIM_SHARED_H
#define SIM_SHARED_H

#include <vector>
#include <iostream>
#include <utility>
#include <cmath>
#include "config.h"
#include "circle_state.h"

constexpr float DEFAULT_CAMERA_ASPECT_RATIO = 4.0f / 3.0f;
constexpr float DEFAULT_PLOT_ASPECT_RATIO = 1.5f;

inline float safeCameraAspectRatio(float cameraAspectRatio) {
    return cameraAspectRatio > 0.0f ? cameraAspectRatio : DEFAULT_CAMERA_ASPECT_RATIO;
}

struct HandSensitivityParams {
    float smoothingAlphaLow;
    float smoothingAlphaHigh;
    float speedThreshold;
    float momentumDeadZone;
    float lowMotionImpulseScale;
    float lowMotionSoftCeilingMul;
    float positionHysteresis;
    float velocityAlphaScale;
};

inline float lerpHandParam(float a, float b, float t) {
    return a + (b - a) * t;
}

inline HandSensitivityParams handSensitivityBypassParams() {
    HandSensitivityParams params{};
    params.smoothingAlphaLow = 1.0f;
    params.smoothingAlphaHigh = 1.0f;
    params.speedThreshold = 0.0f;
    params.momentumDeadZone = 0.0f;
    params.lowMotionImpulseScale = 1.0f;
    params.lowMotionSoftCeilingMul = 4.0f;
    params.positionHysteresis = 0.0f;
    params.velocityAlphaScale = 1.0f;
    return params;
}

inline HandSensitivityParams resolveHandSensitivity(const CircleConfig& cfg) {
    // handSensitivity: 0 = max stabilization, 1 = raw/unfiltered input.
    const float sensitivity = std::max(0.0f, std::min(1.0f, cfg.handSensitivity));
    const float stability = 1.0f - sensitivity;

    if (stability <= 0.0f) {
        return handSensitivityBypassParams();
    }

    constexpr float midAlphaLow = 0.10f;
    constexpr float midAlphaHigh = 0.95f;
    constexpr float midSpeedThreshold = 6.0f;
    constexpr float midDeadZone = 22.0f;
    const HandSensitivityParams bypass = handSensitivityBypassParams();

    HandSensitivityParams params{};
    if (stability <= 0.5f) {
        const float t = stability / 0.5f;
        params.smoothingAlphaLow = lerpHandParam(bypass.smoothingAlphaLow, midAlphaLow, t);
        params.smoothingAlphaHigh = lerpHandParam(bypass.smoothingAlphaHigh, midAlphaHigh, t);
        params.speedThreshold = lerpHandParam(bypass.speedThreshold, midSpeedThreshold, t);
        params.momentumDeadZone = lerpHandParam(bypass.momentumDeadZone, midDeadZone, t);
    } else {
        const float t = (stability - 0.5f) / 0.5f;
        params.smoothingAlphaLow = lerpHandParam(midAlphaLow, 0.008f, t);
        params.smoothingAlphaHigh = lerpHandParam(midAlphaHigh, 0.60f, t);
        params.speedThreshold = lerpHandParam(midSpeedThreshold, 24.0f, t);
        params.momentumDeadZone = lerpHandParam(midDeadZone, 115.0f, t);
    }

    params.lowMotionImpulseScale = 1.0f;
    params.lowMotionSoftCeilingMul = 4.0f;
    params.positionHysteresis = 0.0f;
    params.velocityAlphaScale = 1.0f;
    if (stability > 0.5f) {
        const float t = (stability - 0.5f) / 0.5f;
        params.lowMotionImpulseScale = lerpHandParam(1.0f, 0.02f, t);
        params.lowMotionSoftCeilingMul = lerpHandParam(4.0f, 49.0f, t);
        params.positionHysteresis = lerpHandParam(0.0f, 0.65f, t);
        params.velocityAlphaScale = lerpHandParam(1.0f, 0.15f, t);
    }

    return params;
}

inline float computeHandSmoothingAlpha(float speed, const HandSensitivityParams& params) {
    if (speed > params.speedThreshold) {
        return params.smoothingAlphaHigh;
    }
    if (params.speedThreshold <= 0.0f) {
        return params.smoothingAlphaLow;
    }
    return params.smoothingAlphaLow +
        (params.smoothingAlphaHigh - params.smoothingAlphaLow) * (speed / params.speedThreshold);
}

inline void applyHandSmoothing(
    int rawGridX, int rawGridY,
    float& smoothedX, float& smoothedY,
    int& outX, int& outY,
    bool wasPresent,
    const CircleConfig& cfg,
    float timestep)
{
    const HandSensitivityParams sensitivity = resolveHandSensitivity(cfg);
    const int committedX = outX;
    const int committedY = outY;

    if (wasPresent) {
        float dx = static_cast<float>(rawGridX) - smoothedX;
        float dy = static_cast<float>(rawGridY) - smoothedY;
        float speed = std::sqrt(dx * dx + dy * dy) / timestep;
        float adaptiveAlpha = computeHandSmoothingAlpha(speed, sensitivity);

        smoothedX = adaptiveAlpha * static_cast<float>(rawGridX) + (1.0f - adaptiveAlpha) * smoothedX;
        smoothedY = adaptiveAlpha * static_cast<float>(rawGridY) + (1.0f - adaptiveAlpha) * smoothedY;

        if (sensitivity.positionHysteresis > 0.0f) {
            float commitDx = smoothedX - static_cast<float>(committedX);
            float commitDy = smoothedY - static_cast<float>(committedY);
            if (std::sqrt(commitDx * commitDx + commitDy * commitDy) < sensitivity.positionHysteresis) {
                smoothedX = static_cast<float>(committedX);
                smoothedY = static_cast<float>(committedY);
                outX = committedX;
                outY = committedY;
                return;
            }
        }

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
    const HandSensitivityParams sensitivity = resolveHandSensitivity(cfg);
    float alpha = computeHandSmoothingAlpha(handSpeed, sensitivity) * sensitivity.velocityAlphaScale;
    velX = alpha * instantVelX + (1.0f - alpha) * velX;
    velY = alpha * instantVelY + (1.0f - alpha) * velY;
}

inline float computeMomentumImpulseScale(float velX, float velY, const HandSensitivityParams& params) {
    const float deadZone = params.momentumDeadZone;
    if (params.lowMotionImpulseScale >= 1.0f || deadZone <= 0.0f) {
        return 1.0f;
    }

    const float velMagSq = velX * velX + velY * velY;
    const float deadSq = deadZone * deadZone;
    if (velMagSq <= deadSq) {
        return 0.0f;
    }

    const float softCeilingSq = deadSq * params.lowMotionSoftCeilingMul;
    if (velMagSq >= softCeilingSq) {
        return 1.0f;
    }

    const float t = (velMagSq - deadSq) / (softCeilingSq - deadSq);
    return params.lowMotionImpulseScale + (1.0f - params.lowMotionImpulseScale) * t;
}

inline bool shouldApplyMomentumTransferVelocity(float velX, float velY, float deadZone) {
    if (velX == 0.0f && velY == 0.0f) return false;
    if (deadZone <= 0.0f) return true;
    return velX * velX + velY * velY >= deadZone * deadZone;
}

inline bool shouldApplyMomentumTransferVelocity(float velX, float velY, const HandSensitivityParams& params) {
    return shouldApplyMomentumTransferVelocity(velX, velY, params.momentumDeadZone);
}

inline int scaleRadiusByZ(float z, int baseRadius, const CircleConfig& cfg) {
    if (z != z) z = 0.0f; // nan check
    float zRange = cfg.zMax - cfg.zMin;
    float t = (zRange > 0.0001f) ? (z - cfg.zMin) / zRange : 0.5f;
    t = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
    float scale = cfg.zScaleMin + t * (cfg.zScaleMax - cfg.zScaleMin);
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
    int numCircles = 0;
    int baseCircleRadius = 0;
    LineSegment segments[HandTracking::MAX_SEGMENTS];
    int numSegments = 0;
    int numPresentSegments = 0;

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

    // simulation methods
    virtual bool init(const Config& config, const ImageData* imageData = nullptr, float aspectRatio = DEFAULT_PLOT_ASPECT_RATIO) =0;
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

                int rotation = 0;
                auto cfgIt = g_config.layout.components.find(vpName);
                if (cfgIt != g_config.layout.components.end()) {
                    rotation = ((cfgIt->second.rotation % 4) + 4) % 4;
                }

                float rotX = normX;
                float rotY = normY;
                switch (rotation) {
                    case 1:
                        rotX = normY;
                        rotY = 1.0f - normX;
                        break;
                    case 2:
                        rotX = 1.0f - normX;
                        rotY = 1.0f - normY;
                        break;
                    case 3:
                        rotX = 1.0f - normY;
                        rotY = normX;
                        break;
                    default:
                        break;
                }

                float simX = rotX * domainWidth;
                float simY = (1.0f - rotY) * domainHeight;
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
    virtual const std::vector<float>& getInk() const =0;
    
    // modes
    virtual bool isUsingGPU() const { return false; }

    // Recompute windTunnelStartCell/windTunnelEndCell from config side and normalized span.
    void recomputeWindTunnelCells(const Config& config);

    // Runtime config reload
    virtual void updateSimParams(const Config& config) {}
    virtual void reinitInk(const ImageData* imageData) {}
    virtual void resetFluidState(bool clearInk = true) {}

    // CPU pull mode: remove solid footprints (GPU never writes solids for mouse input)
    virtual void clearMousePullFootprint() {}

    // mouse helpers
    bool isInsideMouseCircle(int i, int j) {
        float dx = (i + 0.5f) - mouseCircleX;
        float dy = (j + 0.5f) - mouseCircleY;
        return sqrt(dx * dx + dy * dy) <= mouseCircleRadius;
    }

    void onMouseDown(int mouseX, int mouseY) {
        isMouseDragging = true;
        mouseCircleX = mouseX;
        mouseCircleY = mouseY;
        mousePrevCircleX = mouseX;
        mousePrevCircleY = mouseY;
        mouseCircleVelX = 0.0f;
        mouseCircleVelY = 0.0f;
        clearMousePullFootprint();
    }

    void onMouseUp() {
        clearMousePullFootprint();
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