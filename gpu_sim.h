#ifndef GPU_SIMULATOR_H
#define GPU_SIMULATOR_H

#include <webgpu/webgpu.h>
#include "isimulator.h"
#include "sim.h"
#include "config.h"

class GPUFluidSimulator : public ISimulator {
public:
    GPUFluidSimulator(const Config& config);
    ~GPUFluidSimulator() override;

    bool initWebGPU(WGPUDevice device, WGPUQueue queue);
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
    WGPUDevice device;
    WGPUQueue queue;
    bool webgpuInitialized;

    WGPUTexture velocityTexture; // RG32Float (x, y velocity)
    WGPUTexture pressureTexture; // R32Float
    WGPUTexture densityTexture; // R32Float
    WGPUTexture solidTexture; // R8Uint
    WGPUTexture inkTexture; // RGBA32Float (all ink components stored together)
    WGPUTexture waterTexture; // R32Float

    // double-buffering
    WGPUTexture newVelocityTexture;
    WGPUTexture newDensityTexture;
    WGPUTexture newInkTexture;

    // views
    WGPUTextureView velocityTextureView;
    WGPUTextureView pressureTextureView;
    WGPUTextureView densityTextureView;
    WGPUTextureView solidTextureView;
    WGPUTextureView inkTextureView;
    WGPUTextureView waterTextureView;
    WGPUTextureView newVelocityTextureView;
    WGPUTextureView newDensityTextureView;
    WGPUTextureView newInkTextureView;

    // misc.
    WGPUSampler sampler;
    WGPUBuffer uniformBuffer;

    // pipeline stuff
    WGPUPipelineLayout advectPipelineLayout;
    WGPUPipelineLayout projectPipelineLayout;
    WGPUPipelineLayout diffusionPipelineLayout;
    WGPUPipelineLayout integratePipelineLayout;

    WGPUBindGroupLayout velocityBindGroupLayout;
    WGPUBindGroupLayout pressureBindGroupLayout;
    WGPUBindGroupLayout densityBindGroupLayout;
    WGPUBindGroupLayout inkBindGroupLayout;
    WGPUBindGroupLayout uniformBindGroupLayout;
    WGPUBindGroupLayout integrateBindGroupLayout;

    WGPUBindGroup velocityBindGroup;
    WGPUBindGroup pressureBindGroup;
    WGPUBindGroup densityBindGroup;
    WGPUBindGroup inkBindGroup;
    WGPUBindGroup uniformBindGroup;
    WGPUBindGroup integrateBindGroup;

    WGPUComputePipeline integratePipeline;

    // sim properties
    int gridX, gridY;
    const Config* config;

    // TEMP
    FluidSimulator cpuSimulator;

    // boilerplate setup and release stuff
    bool initGPUResources();
    bool initTextures();
    bool initSamplers();
    bool initUniformBuffer();
    bool initPipelineLayouts();
    bool initBindGroups();
    void copyInitialDataToGPU();
    void releaseGPUResources();

    // compute steps
    bool createIntegratePipeline();
    void dispatchIntegrate();
    void updateUniformBuffer();

    // utilities
    WGPUShaderModule loadShader(const char* source);
};

#endif