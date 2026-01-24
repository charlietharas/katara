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

    bool isInsideCircle(int i, int j) override;

    // circle state access
    int getCircleX() const override { return circleX; }
    int getCircleY() const override { return circleY; }
    int getCircleRadius() const override { return circleRadius; }

    // ink data accessors
    const std::vector<float>& getRedInk() const override { return cpuSimulator.getRedInk(); }
    const std::vector<float>& getGreenInk() const override { return cpuSimulator.getGreenInk(); }
    const std::vector<float>& getBlueInk() const override { return cpuSimulator.getBlueInk(); }
    bool isInkInitialized() const override { return cpuSimulator.isInkInitialized(); }

    // GPU texture accessors
    bool isUsingGPU() const override { return true; }
    WGPUTexture getVelocityTexture() const { return velocityTexture; }
    WGPUTexture getPressureTexture() const { return pressureTexture; }
    WGPUTexture getDensityTexture() const { return densityTexture; }
    WGPUTexture getSolidTexture() const { return solidTexture; }
    WGPUTexture getInkTexture() const { return inkTexture; }

private:
    WGPUDevice device;
    WGPUQueue queue;
    bool webgpuInitialized;

    WGPUTexture velocityTexture; // RG32Float (x, y velocity)
    WGPUTexture pressureTexture; // R32Float
    WGPUTexture densityTexture; // R32Float
    WGPUTexture solidTexture; // R8Uint
    WGPUTexture inkTexture; // RGBA32Float (all ink components stored together)
    WGPUTexture divergenceTexture; // R32Float

    // double-buffering
    WGPUTexture newVelocityTexture;
    WGPUTexture newDensityTexture;
    WGPUTexture newInkTexture;
    WGPUTexture newPressureTexture;

    // views
    WGPUTextureView velocityTextureView;
    WGPUTextureView pressureTextureView;
    WGPUTextureView densityTextureView;
    WGPUTextureView solidTextureView;
    WGPUTextureView inkTextureView;
    WGPUTextureView divergenceTextureView;
    WGPUTextureView newVelocityTextureView;
    WGPUTextureView newDensityTextureView;
    WGPUTextureView newInkTextureView;
    WGPUTextureView newPressureTextureView;

    // misc.
    WGPUSampler sampler;
    WGPUBuffer uniformBuffer;

    // pipeline stuff
    WGPUPipelineLayout advectPipelineLayout;
    WGPUPipelineLayout integratePipelineLayout;
    WGPUPipelineLayout divergencePipelineLayout;
    WGPUPipelineLayout jacobiPipelineLayout;
    WGPUPipelineLayout velocityUpdatePipelineLayout;
    WGPUPipelineLayout extrapolatePipelineLayout;

    WGPUBindGroupLayout velocityBindGroupLayout;
    WGPUBindGroupLayout pressureBindGroupLayout;
    WGPUBindGroupLayout densityBindGroupLayout;
    WGPUBindGroupLayout inkBindGroupLayout;
    WGPUBindGroupLayout uniformBindGroupLayout;
    WGPUBindGroupLayout integrateBindGroupLayout;
    WGPUBindGroupLayout divergenceBindGroupLayout;
    WGPUBindGroupLayout jacobiBindGroupLayout;
    WGPUBindGroupLayout velocityUpdateBindGroupLayout;
    WGPUBindGroupLayout extrapolateBindGroupLayout;

    WGPUBindGroup velocityBindGroup;
    WGPUBindGroup pressureBindGroup;
    WGPUBindGroup densityBindGroup;
    WGPUBindGroup inkBindGroup;
    WGPUBindGroup uniformBindGroup;

    WGPUBindGroup integrateBindGroup;
    WGPUBindGroup divergenceBindGroup;
    WGPUBindGroup jacobiBindGroup;
    WGPUBindGroup jacobiPingPongBindGroup; // reads newPressure, writes pressure
    WGPUBindGroup velocityUpdateBindGroup;
    WGPUBindGroup extrapolateBindGroup;

    WGPUComputePipeline integratePipeline;
    WGPUComputePipeline divergencePipeline;
    WGPUComputePipeline jacobiPressurePipeline;
    WGPUComputePipeline velocityUpdatePipeline;
    WGPUComputePipeline extrapolatePipeline;

    // advect pipelines
    WGPUComputePipeline advectVelocityPipeline;
    WGPUComputePipeline advectDensityPipeline;
    WGPUComputePipeline advectInkPipeline;

    WGPUPipelineLayout advectVelocityPipelineLayout;
    WGPUPipelineLayout advectDensityPipelineLayout;
    WGPUPipelineLayout advectInkPipelineLayout;

    WGPUBindGroupLayout advectVelocityBindGroupLayout;
    WGPUBindGroupLayout advectDensityBindGroupLayout;
    WGPUBindGroupLayout advectInkBindGroupLayout;

    WGPUBindGroup advectVelocityBindGroup;
    WGPUBindGroup advectDensityBindGroup;
    WGPUBindGroup advectInkBindGroup;

    // boundary condition pipeline
    WGPUPipelineLayout boundaryPipelineLayout;
    WGPUBindGroupLayout boundaryBindGroupLayout;
    WGPUBindGroup boundaryBindGroup;
    WGPUComputePipeline boundaryPipeline;

    // boundary neighbors pipeline (clears velocity components adjacent to solids)
    WGPUPipelineLayout boundaryNeighborsPipelineLayout;
    WGPUBindGroupLayout boundaryNeighborsBindGroupLayout;
    WGPUBindGroup boundaryNeighborsBindGroup;
    WGPUComputePipeline boundaryNeighborsPipeline;

    // vorticity pipelines
    WGPUPipelineLayout vorticityComputePipelineLayout;
    WGPUPipelineLayout vorticityApplyPipelineLayout;
    WGPUBindGroupLayout vorticityComputeBindGroupLayout;
    WGPUBindGroupLayout vorticityApplyBindGroupLayout;
    WGPUBindGroup vorticityComputeBindGroup;
    WGPUBindGroup vorticityApplyBindGroup;
    WGPUComputePipeline vorticityComputePipeline;
    WGPUComputePipeline vorticityApplyPipeline;
    WGPUTexture curlTexture;
    WGPUTextureView curlTextureView;

    // sim properties
    int gridX, gridY;
    float cellSize;
    const Config* config;

    // Circle state (matching CPU implementation)
    int circleX, circleY;
    int prevCircleX, prevCircleY;
    float circleVelX, circleVelY;
    int circleRadius;
    bool isDragging;
    bool circleWasMoved;

    // Circle configuration parameters
    float momentumTransferCoeff;
    float momentumTransferRadius;

    // Circle pipeline
    WGPUPipelineLayout circlePipelineLayout;
    WGPUBindGroupLayout circleBindGroupLayout;
    WGPUBindGroup circleBindGroup;
    WGPUComputePipeline circlePipeline;

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
    void dispatchIntegrate(WGPUCommandEncoder encoder);
    void updateUniformBuffer();
    bool createProjectionPipelines();
    void dispatchProjection(WGPUCommandEncoder encoder);
    bool createExtrapolatePipeline();
    void dispatchExtrapolate(WGPUCommandEncoder encoder);

    // advect step
    bool createAdvectPipelines();
    void dispatchAdvect(WGPUCommandEncoder encoder);

    // boundary conditions
    bool createBoundaryPipeline();
    void dispatchBoundaryConditions(WGPUCommandEncoder encoder);

    // boundary neighbors (clears velocity components adjacent to solids)
    bool createBoundaryNeighborsPipeline();
    void dispatchBoundaryNeighbors(WGPUCommandEncoder encoder);

    // vorticity confinement
    bool createVorticityPipelines();
    void dispatchVorticity(WGPUCommandEncoder encoder);

    // circle interactivity
    bool createCirclePipeline();
    void dispatchCircle(WGPUCommandEncoder encoder);
    void moveCircle(int newGridX, int newGridY);

    // utilities
    WGPUShaderModule loadShader(const char* source);
};

#endif