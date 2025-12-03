#ifndef ISIMULATOR_H
#define ISIMULATOR_H

#include <vector>

class ISimulator {
public:
    virtual ~ISimulator() = default;

    // simulation methods
    virtual void init(bool imageLoaded) = 0;
    virtual void update() = 0;

    // mouse interaction
    virtual void onMouseDown(int gridX, int gridY) = 0;
    virtual void onMouseDrag(int gridX, int gridY) = 0;
    virtual void onMouseUp() = 0;

    // grid dimensions
    virtual int getGridX() const = 0;
    virtual int getGridY() const = 0;
    virtual float getCellSize() const = 0;

    // domain dimensions
    virtual float getDomainWidth() const = 0;
    virtual float getDomainHeight() const = 0;

    // data accessors
    virtual const std::vector<float>& getVelocityX() const = 0;
    virtual const std::vector<float>& getVelocityY() const = 0;
    virtual const std::vector<float>& getPressure() const = 0;
    virtual const std::vector<float>& getDensity() const = 0;
    virtual const std::vector<float>& getSolid() const = 0;
    virtual const std::vector<float>& getRedInk() const { static std::vector<float> empty; return empty; }
    virtual const std::vector<float>& getGreenInk() const { static std::vector<float> empty; return empty; }
    virtual const std::vector<float>& getBlueInk() const { static std::vector<float> empty; return empty; }
    virtual const std::vector<float>& getWaterContent() const { static std::vector<float> empty; return empty; }

    // misc
    virtual bool isInkInitialized() const { return false; }
    virtual bool isInsideCircle(int i, int j) = 0;
};

#endif