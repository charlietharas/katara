#ifndef ISIMULATOR_H
#define ISIMULATOR_H

#include <vector>
#include <iostream>
#include <utility>
#include "config.h"
#include "circle_state.h"

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

#ifdef ENABLE_MOUSE_INPUT
    int circleX = 0, circleY = 0;
    int prevCircleX = 0, prevCircleY = 0;
    float circleVelX = 0.0f, circleVelY = 0.0f;
    int circleRadius = 0;
    bool isDragging = false;
#else
    CircleState circles[HandTracking::MAX_CIRCLES];
    int baseCircleRadius = 0;
    LineSegment segments[HandTracking::MAX_SEGMENTS];
    int numSegments = 0;
#endif

    // wind tunnel state
    float windTunnelStart; // 0-1 (pass this one in)
    float windTunnelEnd;
    int windTunnelStartCell = 0; // the actual grid coords used in sim; calculated in init
    int windTunnelEndCell = 0;
    int pipeHeight = 0;
    int windTunnelSide; // 0, 1, 2, 3 = left, top, bottom, right; -1 = disabled
    float windTunnelSpeed; // magnitude; direction inferred

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

#ifdef ENABLE_MOUSE_INPUT
    // mouse helpers
    bool isInsideCircle(int i, int j) {
        float dx = (i + 0.5f) - circleX;
        float dy = (j + 0.5f) - circleY;
        return sqrt(dx * dx + dy * dy) <= circleRadius;
    }

    void onMouseDown(int mouseX, int mouseY) {
        isDragging = true;
    }

    void onMouseUp() {
        isDragging = false;
    }

    void onMouseDrag(int mouseX, int mouseY) {
        if (isDragging) {
            // clamp circle to bounds
            int newX = std::max(circleRadius, std::min(mouseX, gridX - circleRadius - 1));
            int newY = std::max(circleRadius, std::min(mouseY, gridY - circleRadius - 1));
            if (newX != circleX || newY != circleY) {
                moveCircle(newX, newY);
            }
        }
    }

    virtual void moveCircle(int newGridX, int newGridY) {}
#else
    // fingertip mode helpers (fuck it, they're always here. I'm too tired for this)
    virtual void updateCircles(const FingertipData* fingertips, int count) {}
    virtual void updateLineSegments(const FingertipData* landmarks, int count) {}
#endif
};

#endif