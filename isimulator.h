#ifndef ISIMULATOR_H
#define ISIMULATOR_H

#include <vector>
#include <iostream>
#include <utility>
#include "config.h"

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

    // circle state (also grid)
    int circleX = 0, circleY = 0;
    int prevCircleX = 0, prevCircleY = 0;
    float circleVelX = 0.0f, circleVelY = 0.0f;
    int circleRadius = 0;
    bool isDragging = false;

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

    // mouse helpers (declared)
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

    // this one needs to be implemented by children
    virtual void moveCircle(int newGridX, int newGridY) =0;
    
};

#endif