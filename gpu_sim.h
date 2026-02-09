#ifndef GPU_SIMULATOR_H
#define GPU_SIMULATOR_H

#include <webgpu/webgpu.h>
#include "wgpu_boilerplate.h"
#include "isimulator.h"
#include "sim.h"
#include "config.h"

// SIM PARAMS UNIFORM
struct alignas(16) SimParams {
    int gridX;
    int gridY;
    float cellSize;
    float timestep;
    float gravity;
    float vorticity;
    float vorticityLen;
    float projectionIters;
    float density;
    int windTunnelSide;
    int windTunnelStart;
    int windTunnelEnd;
    float windTunnelSpeed;
    int circleX;
    int circleY;
    int prevCircleX;
    int prevCircleY;
    int circleRadius;
    float circleVelX;
    float circleVelY;
    float momentumTransferStrength;
    float momentumTransferRadius;
    int circleWasMoved;
    float halfCellSize;
    float pad0;
    float pad1;
    float pad2;
};

// ring buffer histogram slots
enum HistogramSlotState {
    HistogramSlot_Free,
    HistogramSlot_Computing,
    HistogramSlot_Binning,
    HistogramSlot_Ready
};

struct HistogramSlot {
    WGPUBuffer minMaxBuffer;
    WGPUBuffer minMaxStagingBuffer;
    WGPUBuffer histogramBinBuffer;
    WGPUBuffer histogramStagingBuffer;
    WGPUBuffer minMaxUniformBuffer;
    WGPUBindGroup bindGroupMinMax;
    WGPUBindGroup bindGroupHistogramBins;
    HistogramSlotState state;
    float pendingPressureMinMax[2];
    float pendingVelocityMinMax[2];
    int pendingHistogramBins[128];
};

class GPUSimulator : public WGPUBoilerplate, public ISimulator {
public:
    GPUSimulator(const Config& config);
    ~GPUSimulator();

    bool init(const Config& config, const ImageData* imageData = nullptr, float aspectRatio = 1.5f) override;
    bool accessGPUPipeline(WGPUDevice device, WGPUQueue queue); // GPU-specific init boilerplate
    void copyInitialDataToGPU();
    void update() override;

    // fields
    // TODO add shitty slow cpu callback (test: would this work with CPU/GPU rendering/sim?)
    CPU_SIM_GETTER(getVelocityX)
    CPU_SIM_GETTER(getVelocityY)
    CPU_SIM_GETTER(getPressure)
    CPU_SIM_GETTER(getDensity)
    CPU_SIM_GETTER(getSolid)
    CPU_SIM_GETTER(getRedInk)
    CPU_SIM_GETTER(getGreenInk)
    CPU_SIM_GETTER(getBlueInk)

    // modes
    bool isUsingGPU() const override { return true; }
    
    // GPU textures
    WGPUTexture getVelocityTexture() const { return velocityTexture; }
    WGPUTexture getPressureTexture() const { return pressureTexture; }
    WGPUTexture getDensityTexture() const { return densityTexture; }
    WGPUTexture getSolidTexture() const { return solidTexture; }
    WGPUTexture getInkTexture() const { return inkTexture; }

    // histogram data access (for renderer)
    static const int HISTOGRAM_RING_SIZE = 4;
    bool getHistogramData(int& readySlot, const float*& pressureMinMax, const float*& velocityMinMax, const int*& histogramBins) const;
    void advanceHistogramReadIndex() const;

private:
    // for init (we borrow the CPU's init() and copy to device)
    Simulator cpuSimulator;
    const Config* config = nullptr;

    // workgroup size (initialized to ceil(gridDim / 16))
    uint32_t workgroupX = 0, workgroupY = 0;

    // circle state
    bool circleWasMoved = false; // workaround for CPU sim; passed into uniform buffer

    // histogram state
    mutable HistogramSlot histogramSlots[HISTOGRAM_RING_SIZE];
    mutable int histogramWriteIndex = 0;
    mutable int histogramReadIndex = 0;

    // webgpu core
    WGPUSampler sampler = nullptr;
    WGPUBuffer uniformBuffer = nullptr;

    // textures and views
    bool inkInitialized = false;
    DECLARE_TEXTURE_AND_VIEW(velocity)
    DECLARE_TEXTURE_AND_VIEW(pressure)
    DECLARE_TEXTURE_AND_VIEW(density)
    DECLARE_TEXTURE_AND_VIEW(solid)
    DECLARE_TEXTURE_AND_VIEW(ink)
    DECLARE_TEXTURE_AND_VIEW(divergence)
    DECLARE_TEXTURE_AND_VIEW(curl)
    DECLARE_TEXTURE_AND_VIEW(newVelocity)
    DECLARE_TEXTURE_AND_VIEW(newDensity)
    DECLARE_TEXTURE_AND_VIEW(newInk)
    DECLARE_TEXTURE_AND_VIEW(newPressure)

    // pipeline resources
    DECLARE_PIPELINE_RESOURCES(integrate)
    DECLARE_PIPELINE_RESOURCES(divergence)
    DECLARE_PIPELINE_RESOURCES(jacobi)
    DECLARE_PIPELINE_RESOURCES(jacobiPingPong)
    DECLARE_PIPELINE_RESOURCES(velocityUpdate)
    DECLARE_PIPELINE_RESOURCES(extrapolate)
    DECLARE_PIPELINE_RESOURCES(advectVelocity)
    DECLARE_PIPELINE_RESOURCES(advectDensity)
    DECLARE_PIPELINE_RESOURCES(advectInk)
    DECLARE_PIPELINE_RESOURCES(boundary)
    DECLARE_PIPELINE_RESOURCES(vorticityCompute)
    DECLARE_PIPELINE_RESOURCES(vorticityApply)
    DECLARE_PIPELINE_RESOURCES(circle)
    DECLARE_PIPELINE_RESOURCES(pressureMinMax)
    DECLARE_PIPELINE_RESOURCES(histogramBins)

    // updated with SimParams
    void updateUniformBufferSim();

    // compute dispatch
    void dispatchComputePass(WGPUCommandEncoder encoder, WGPUComputePipeline pipeline, WGPUBindGroup bindGroup);
    void dispatchIntegrate(WGPUCommandEncoder encoder);
    void dispatchProjection(WGPUCommandEncoder encoder);
    void dispatchExtrapolate(WGPUCommandEncoder encoder);
    void dispatchAdvect(WGPUCommandEncoder encoder);
    void dispatchBoundaryConditions(WGPUCommandEncoder encoder);
    void dispatchVorticity(WGPUCommandEncoder encoder);
    void dispatchCircle(WGPUCommandEncoder encoder);
    void dispatchPressureMinMax(int slotIndex);
    void dispatchHistogramBins(int slotIndex);
    void dispatchHistogramCompute(const struct HistogramDispatchDesc& desc); // async helper

    // histogram async callbacks
    void onMinMaxMapped(WGPUMapAsyncStatus status, int slotIndex);
    void onHistogramBinsMapped(WGPUMapAsyncStatus status, int slotIndex);

    // circle movement
    void moveCircle(int newGridX, int newGridY) override;

    // gpu resource initialization helpers
    bool createTexture(const TextureDesc& desc, WGPUTexture& texture, WGPUTextureView& view);
    void copyTextureDeviceToDevice(WGPUCommandEncoder encoder, WGPUTexture srcTexture, WGPUTexture dstTexture);
    void createUniformBufferPipelineLayoutEntry(WGPUBindGroupLayoutEntry* entries);
    WGPUBindGroupLayoutDescriptor createBindGroupLayoutDescriptor(int count, WGPUBindGroupLayoutEntry* entries);
    WGPUBindGroupDescriptor createBindGroupDescriptor(int count, WGPUBindGroupEntry* entries, WGPUBindGroupLayout layout);
    WGPUComputePipeline createComputePipeline(const char* shaderFile, const char* entryPoint, WGPUPipelineLayout layout);
    WGPUBindGroupLayoutEntry createStorageTextureLayoutEntry(int binding, WGPUStorageTextureAccess access, WGPUTextureFormat format);
    WGPUBindGroupLayoutEntry createStorageBufferLayoutEntry(int binding, size_t minSize);
    WGPUBindGroupEntry createStorageBufferBindGroupEntry(int binding, WGPUBuffer buffer, size_t size);

    // gpu resource initialization boilerplate
    bool initSimData(const Config& cfg, const ImageData* imageData, float aspectRatio);
    bool initUniformBuffer();
    bool initTextures();
    bool initPipelineLayouts();
    bool initBindGroups();
    bool initPipelines();
};

#endif 