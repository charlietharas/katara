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

// SIM PARAMS UNIFORM
// Circle/segment payloads live in storage buffers (see CircleGPU/SegmentGPU) so this
// stays small and every shader can declare just the prefix of fields it reads.
struct alignas(16) SimParams {
    int gridX;
    int gridY;
    float overrelaxationCoefficient;
    float timestep;
    float cellSize;
    float halfCellSize;
    int windTunnelSide;
    int windTunnelStart;
    int windTunnelEnd;
    float windTunnelSpeed;
    int edges;
    float vorticity;
    float vorticityLen;
    int inputMode;
    int numPresentSegments;
    int numCircles;
    int numSegments;
    float momentumTransferStrength;
    float momentumTransferRadius;
    float momentumTransferDeadZone;
    float momentumLowMotionScale;
    float momentumLowMotionSoftCeilingMul;
    int clearDensityOnVacate;
    int _pad2;
};
static_assert(sizeof(SimParams) % 16 == 0, "SimParams invalid alignment");

// Input-pass storage buffer payloads (must match compute_input.wgsl)
constexpr uint32_t CIRCLE_FLAG_PRESENT = 1u;
constexpr uint32_t CIRCLE_FLAG_WAS_PRESENT = 2u;

struct alignas(8) CircleGPU {
    float posX, posY;
    float prevPosX, prevPosY;
    float velX, velY;
    float radius;
    uint32_t flags;
};
static_assert(sizeof(CircleGPU) == 32, "CircleGPU must match WGSL layout");

struct alignas(8) SegmentGPU {
    float startX, startY;
    float endX, endY;
    float startVelX, startVelY;
    float endVelX, endVelY;
    float startRadius, endRadius;
    float cullRadiusSq;
    float _pad;
};
static_assert(sizeof(SegmentGPU) == 48, "SegmentGPU must match WGSL layout");

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
    DECLARE_TEXTURE_AND_VIEW(ink)
    DECLARE_TEXTURE_AND_VIEW(divergence)
    DECLARE_TEXTURE_AND_VIEW(curl)
    DECLARE_TEXTURE_AND_VIEW(newVelocity)
    DECLARE_TEXTURE_AND_VIEW(newDensity)
    DECLARE_TEXTURE_AND_VIEW(newInk)
    DECLARE_TEXTURE_AND_VIEW(newPressure)
    DECLARE_TEXTURE_AND_VIEW(newSolid)

    // pipeline resources
    DECLARE_PIPELINE_RESOURCES(divergence)
    DECLARE_PIPELINE_RESOURCES(jacobi)
    DECLARE_PIPELINE_RESOURCES(jacobiPingPong)
    DECLARE_PIPELINE_RESOURCES(gs)
    WGPUComputePipeline gsBlackPipeline = nullptr;
    DECLARE_PIPELINE_RESOURCES(velocityUpdate)
    DECLARE_PIPELINE_RESOURCES(extrapolate)
    DECLARE_PIPELINE_RESOURCES(advectVelocity)
    DECLARE_PIPELINE_RESOURCES(advectDensity)
    DECLARE_PIPELINE_RESOURCES(advectInk)
    DECLARE_PIPELINE_RESOURCES(boundary)
    DECLARE_PIPELINE_RESOURCES(vorticityCompute)
    DECLARE_PIPELINE_RESOURCES(vorticityApply)
    DECLARE_PIPELINE_RESOURCES(input)
    DECLARE_PIPELINE_RESOURCES(pressureMinMax)
    DECLARE_PIPELINE_RESOURCES(histogramBins)

    // input-pass storage buffers (circles + skeleton segments)
    WGPUBuffer circleBuffer = nullptr;
    WGPUBuffer segmentBuffer = nullptr;

    // updated with SimParams
    void updateUniformBufferSim();
    // packs the compacted circle/segment payloads, returns how many of each were uploaded
    void uploadInputBuffers(int& circleCount, int& segmentCount);

    // compute dispatch
    void dispatchComputePass(WGPUCommandEncoder encoder, WGPUComputePipeline pipeline, WGPUBindGroup bindGroup);
    void dispatchProjection(WGPUCommandEncoder encoder);
    void dispatchExtrapolate(WGPUCommandEncoder encoder);
    void dispatchAdvect(WGPUCommandEncoder encoder);
    void dispatchBoundaryConditions(WGPUCommandEncoder encoder);
    void dispatchVorticity(WGPUCommandEncoder encoder);
    void dispatchInput(WGPUCommandEncoder encoder);
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
    WGPUBindGroupLayoutEntry createStorageBufferLayoutEntry(int binding, size_t minSize, bool readOnly = false);
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