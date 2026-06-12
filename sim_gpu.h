#ifndef SIM_GPU_H
#define SIM_GPU_H

#include <cstdint>
#include <webgpu/webgpu.h>
#include "boilerplate.h"
#include "sim_shared.h"
#include "sim_cpu.h"
#include "config.h"
#include "circle_state.h"

struct MinMaxUniform {
    float pressMin, pressMax, velMin, velMax;
};

struct alignas(16) I32Vec4 {
    int x, y, z, w;
};

struct alignas(16) F32Vec4 {
    float x, y, z, w;
};

inline int& simPackedI32(I32Vec4* arr, int index) {
    return reinterpret_cast<int*>(arr)[index];
}

inline const int& simPackedI32(const I32Vec4* arr, int index) {
    return reinterpret_cast<const int*>(arr)[index];
}

inline float& simPackedF32(F32Vec4* arr, int index) {
    return reinterpret_cast<float*>(arr)[index];
}

inline const float& simPackedF32(const F32Vec4* arr, int index) {
    return reinterpret_cast<const float*>(arr)[index];
}

// SIM PARAMS UNIFORM
struct alignas(16) SimParams {
    static constexpr int CIRCLE_VEC4_COUNT = (HandTracking::MAX_CIRCLES + 3) / 4;
    static constexpr int SEGMENT_VEC4_COUNT = (HandTracking::MAX_SEGMENTS + 3) / 4;

    int gridX;
    int gridY;
    float cellSize;
    float halfCellSize;
    float timestep;
    int windTunnelSide;
    int windTunnelStart;
    int windTunnelEnd;
    float windTunnelSpeed;
    int edges; // 4-bit mask: left(8), top(4), bottom(2), right(1)
    float momentumTransferStrength;
    float momentumTransferRadius;
    float momentumTransferDeadZone;
    float vorticity;
    float vorticityLen;
    int _pad0;
    // track 21 landmarks per hand
    I32Vec4 circleX[CIRCLE_VEC4_COUNT], circleY[CIRCLE_VEC4_COUNT];
    I32Vec4 prevCircleX[CIRCLE_VEC4_COUNT], prevCircleY[CIRCLE_VEC4_COUNT];
    F32Vec4 circleZ[CIRCLE_VEC4_COUNT];
    I32Vec4 circleScaledRadius[CIRCLE_VEC4_COUNT];
    I32Vec4 circlePresent[CIRCLE_VEC4_COUNT];
    I32Vec4 circleWasPresent[CIRCLE_VEC4_COUNT];
    F32Vec4 circleVelX[CIRCLE_VEC4_COUNT], circleVelY[CIRCLE_VEC4_COUNT];
    int numCircles;
    int baseCircleRadius; // base radius from config

    // hand skeleton connections (23 per hand)
    I32Vec4 segmentStartX[SEGMENT_VEC4_COUNT], segmentStartY[SEGMENT_VEC4_COUNT];
    I32Vec4 segmentEndX[SEGMENT_VEC4_COUNT], segmentEndY[SEGMENT_VEC4_COUNT];
    I32Vec4 segmentPrevStartX[SEGMENT_VEC4_COUNT], segmentPrevStartY[SEGMENT_VEC4_COUNT];
    I32Vec4 segmentPrevEndX[SEGMENT_VEC4_COUNT], segmentPrevEndY[SEGMENT_VEC4_COUNT];
    F32Vec4 segmentStartRadius[SEGMENT_VEC4_COUNT], segmentEndRadius[SEGMENT_VEC4_COUNT];
    F32Vec4 segmentPrevStartRadius[SEGMENT_VEC4_COUNT], segmentPrevEndRadius[SEGMENT_VEC4_COUNT];
    I32Vec4 segmentPresent[SEGMENT_VEC4_COUNT];
    I32Vec4 segmentWasPresent[SEGMENT_VEC4_COUNT];
    int numSegments;
    int inputMode;
    int numPresentSegments;
    float momentumLowMotionScale;
    float momentumLowMotionSoftCeilingMul;
    int _padEnd;
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
    float pendingDensityMinMax[2];
    uint32_t pendingDensitySumScaled;
    uint32_t pendingFluidCellCount;
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
    CPU_SIM_GETTER(getVelocityX)
    CPU_SIM_GETTER(getVelocityY)
    CPU_SIM_GETTER(getPressure)
    CPU_SIM_GETTER(getDensity)
    CPU_SIM_GETTER(getSolid)
    CPU_SIM_GETTER(getInk)

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
    bool getHistogramData(
        int& readySlot,
        const float*& pressureMinMax,
        const float*& velocityMinMax,
        const float*& densityMinMax,
        const int*& histogramBins,
        uint32_t& densitySumScaled,
        uint32_t& fluidCellCount
    ) const;
    void advanceHistogramReadIndex() const;

private:
    // for init (we borrow the CPU's init() and copy to device)
    Simulator cpuSimulator;
    const Config* config = nullptr;

    // workgroup size (initialized to ceil(gridDim / 16))
    uint32_t workgroupX = 0, workgroupY = 0;

    float momentumTransferStrength;
    float momentumTransferRadius;

    // histogram state
    mutable HistogramSlot histogramSlots[HISTOGRAM_RING_SIZE] = {};
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
    DECLARE_PIPELINE_RESOURCES(lineSegment)
    DECLARE_PIPELINE_RESOURCES(pressureMinMax)
    DECLARE_PIPELINE_RESOURCES(histogramBins)

    // updated with SimParams
    void updateUniformBufferSim();

    // compute dispatch
    void dispatchComputePass(WGPUCommandEncoder encoder, WGPUComputePipeline pipeline, WGPUBindGroup bindGroup);
    void dispatchProjection(WGPUCommandEncoder encoder);
    void dispatchExtrapolate(WGPUCommandEncoder encoder);
    void dispatchAdvect(WGPUCommandEncoder encoder);
    void dispatchBoundaryConditions(WGPUCommandEncoder encoder);
    void dispatchVorticity(WGPUCommandEncoder encoder);
    void dispatchCircle(WGPUCommandEncoder encoder);
    void dispatchLineSegments(WGPUCommandEncoder encoder);
    void dispatchPressureMinMax(int slotIndex);
    void dispatchHistogramBins(int slotIndex);
    void dispatchHistogramCompute(const struct HistogramDispatchDesc& desc); // async helper

    int scaleRadiusByZ(float z, int baseRadius);

    // histogram async callbacks
    void onMinMaxMapped(WGPUMapAsyncStatus status, int slotIndex);
    void onHistogramBinsMapped(WGPUMapAsyncStatus status, int slotIndex);

    void moveCircle(int newGridX, int newGridY) override;
    void updateCircles(const FingertipData* fingertips, int count) override;
    void updateLineSegments(const FingertipData* landmarks, int count) override;

    // Runtime config reload
    void updateSimParams(const Config& config) override;
    void reinitInk(const ImageData* imageData) override;
    void resetFluidState(bool clearInk = true) override;

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
    void syncGridFromCpu();
    void releaseSimGridResources();
    bool rebuildGridResources();
    void zeroScalarGpuTexture(WGPUTexture texture);
    void uploadScalarGpuFieldsFromCpu();
    bool initUniformBuffer();
    bool initSimTextures();
    bool initHistogramResources();
    bool initTextures();
    bool initPipelineLayouts();
    bool initBindGroups();
    bool initPipelines();
};

#endif 