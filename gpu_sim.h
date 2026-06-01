#ifndef GPU_SIMULATOR_H
#define GPU_SIMULATOR_H

#include <webgpu/webgpu.h>
#include "boilerplate.h"
#include "isimulator.h"
#include "sim.h"
#include "config.h"
#include "circle_state.h"

struct MinMaxUniform {
    float pressMin, pressMax, velMin, velMax;
};

// SIM PARAMS UNIFORM
struct alignas(16) SimParams {
    int gridX;
    int gridY;
    float cellSize;
    float halfCellSize;
    float timestep;
    float density;
    float gravity;
    float projectionIters;
    int windTunnelSide;
    int windTunnelStart;
    int windTunnelEnd;
    float windTunnelSpeed;
    float momentumTransferStrength;
    float momentumTransferRadius;
    float vorticity;
    float vorticityLen;
#ifdef ENABLE_MOUSE_INPUT
    // we only need to track single circle state
    int circleX, circleY, prevCircleX, prevCircleY;
    int circleRadius;
    int pad0, pad1, pad2;
#else
    // track 21 landmarks per hand
    int circleX[HandTracking::MAX_CIRCLES], circleY[HandTracking::MAX_CIRCLES];
    int prevCircleX[HandTracking::MAX_CIRCLES], prevCircleY[HandTracking::MAX_CIRCLES];
    float circleZ[HandTracking::MAX_CIRCLES]; // we use this to scale radii by distance to wrist
    int circleScaledRadius[HandTracking::MAX_CIRCLES]; // pre-scaled
    int circlePresent[HandTracking::MAX_CIRCLES];
    int circleWasPresent[HandTracking::MAX_CIRCLES];
    int numCircles;
    int baseCircleRadius; // base radius from config

    // hand skeleton connections (23 per hand)
    int segmentStartX[HandTracking::MAX_SEGMENTS], segmentStartY[HandTracking::MAX_SEGMENTS];
    int segmentEndX[HandTracking::MAX_SEGMENTS], segmentEndY[HandTracking::MAX_SEGMENTS];
    int segmentPrevStartX[HandTracking::MAX_SEGMENTS], segmentPrevStartY[HandTracking::MAX_SEGMENTS];
    int segmentPrevEndX[HandTracking::MAX_SEGMENTS], segmentPrevEndY[HandTracking::MAX_SEGMENTS];
    float segmentStartRadius[HandTracking::MAX_SEGMENTS], segmentEndRadius[HandTracking::MAX_SEGMENTS];
    float segmentPrevStartRadius[HandTracking::MAX_SEGMENTS], segmentPrevEndRadius[HandTracking::MAX_SEGMENTS];
    int segmentPresent[HandTracking::MAX_SEGMENTS];
    int segmentWasPresent[HandTracking::MAX_SEGMENTS];
    int numSegments;
    int pad0, pad1, pad2;
#endif
};
static_assert(sizeof(SimParams) % 16 == 0, "SimParams invalid alignment");

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
    // TODO LATER add shitty slow cpu callback (test: would this work with CPU/GPU rendering/sim?)
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

#ifndef ENABLE_MOUSE_INPUT
    int numCircles = 0;
#endif
    float momentumTransferStrength;
    float momentumTransferRadius;

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
    DECLARE_TEXTURE_AND_VIEW(solidStaging)
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
#ifndef ENABLE_MOUSE_INPUT
    DECLARE_PIPELINE_RESOURCES(lineSegment)
#endif
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
#ifndef ENABLE_MOUSE_INPUT
    void dispatchLineSegments(WGPUCommandEncoder encoder);
#endif
    void dispatchPressureMinMax(int slotIndex);
    void dispatchHistogramBins(int slotIndex);
    void dispatchHistogramCompute(const struct HistogramDispatchDesc& desc); // async helper

    int scaleRadiusByZ(float z, int baseRadius);

    // histogram async callbacks
    void onMinMaxMapped(WGPUMapAsyncStatus status, int slotIndex);
    void onHistogramBinsMapped(WGPUMapAsyncStatus status, int slotIndex);

#ifdef ENABLE_MOUSE_INPUT
    void moveCircle(int newGridX, int newGridY) override;
#else
    void updateCircles(const FingertipData* fingertips, int count) override;
    void updateLineSegments(const FingertipData* landmarks, int count) override;
#endif

    // Runtime config reload
    void updateSimParams(const Config& config) override;
    void reinitInk(const ImageData* imageData) override;
    void resetFluidState() override;

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