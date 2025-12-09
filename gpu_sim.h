#ifndef GPU_SIMULATOR_H
#define GPU_SIMULATOR_H

#include "isimulator.h"
#include "sim.h"
#include "config.h"

// PLACEHOLDER: CURRENTLY WRAPS CPU SIMULATOR
class GPUFluidSimulator : public ISimulator {
public:
    GPUFluidSimulator(const Config& config);
    ~GPUFluidSimulator() override;

    // simulation methods
    void init(const Config& config, const ImageData* imageData = nullptr) override;
    void update() override;
    // mouse interaction
    void onMouseDown(int gridX, int gridY) override;
    void onMouseDrag(int gridX, int gridY) override;
    void onMouseUp() override;

    // grid params
    int getGridX() const override { return cpuSimulator.getGridX(); }
    int getGridY() const override { return cpuSimulator.getGridY(); }
    float getCellSize() const override { return cpuSimulator.getCellSize(); }

    float getDomainWidth() const override { return cpuSimulator.getDomainWidth(); }
    float getDomainHeight() const override { return cpuSimulator.getDomainHeight(); }

    // data accessors
    const std::vector<float>& getVelocityX() const override { return cpuSimulator.getVelocityX(); }
    const std::vector<float>& getVelocityY() const override { return cpuSimulator.getVelocityY(); }
    const std::vector<float>& getPressure() const override { return cpuSimulator.getPressure(); }
    const std::vector<float>& getDensity() const override { return cpuSimulator.getDensity(); }
    const std::vector<float>& getSolid() const override { return cpuSimulator.getSolid(); }

    bool isInsideCircle(int i, int j) override { return cpuSimulator.isInsideCircle(i, j); }

    // ink data accessors
    const std::vector<float>& getRedInk() const override { return cpuSimulator.getRedInk(); }
    const std::vector<float>& getGreenInk() const override { return cpuSimulator.getGreenInk(); }
    const std::vector<float>& getBlueInk() const override { return cpuSimulator.getBlueInk(); }
    bool isInkInitialized() const override { return cpuSimulator.isInkInitialized(); }
private:
    FluidSimulator cpuSimulator;
};

#endif