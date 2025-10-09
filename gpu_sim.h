#ifndef GPU_SIMULATOR_H
#define GPU_SIMULATOR_H

#include "isimulator.h"
#include "sim.h"

// PLACEHOLDER: CURRENTLY WRAPS CPU SIMULATOR
class GPUFluidSimulator : public ISimulator {
public:
    GPUFluidSimulator();
    ~GPUFluidSimulator() override;

    // simulation methods
    void init() override;
    void update() override;
    void reset() override;

    // grid params
    int getGridX() const override { return cpuSimulator.getGridX(); }
    int getGridY() const override { return cpuSimulator.getGridY(); }
    float getCellSize() const override { return cpuSimulator.getCellSize(); }

    // data accessors
    const std::vector<float>& getVelocityX() const override { return cpuSimulator.getVelocityX(); }
    const std::vector<float>& getVelocityY() const override { return cpuSimulator.getVelocityY(); }
    const std::vector<float>& getPressure() const override { return cpuSimulator.getPressure(); }
    const std::vector<float>& getDensity() const override { return cpuSimulator.getDensity(); }
    const std::vector<float>& getSolid() const override { return cpuSimulator.getSolid(); }

private:
    FluidSimulator cpuSimulator;
};

#endif