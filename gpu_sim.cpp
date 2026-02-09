#include "wgpu_boilerplate.h"
#include "gpu_sim.h"
#include <iostream>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include <limits>

// CONSTRUCTOR :150

// UPDATE SIM PARAMS UNIFORM
void GPUSimulator::updateUniformBufferSim() {
    SimParams params = {};
    params.gridX = gridX;
    params.gridY = gridY;
    params.cellSize = cellSize;
    params.timestep = config->simulation.timestep;
    params.gravity = config->simulation.gravity;
    params.vorticity = config->simulation.vorticity.strength;
    params.vorticityLen = config->simulation.vorticity.lengthScale;
    params.projectionIters = config->simulation.projection.iterations;
    params.density = config->simulation.fluidDensity;

    params.windTunnelSide = config->simulation.windTunnel.side;
    params.windTunnelStart = windTunnelStartCell;
    params.windTunnelEnd = windTunnelEndCell;
    params.windTunnelSpeed = config->simulation.windTunnel.velocity;

    params.circleX = circleX;
    params.circleY = circleY;
    params.prevCircleX = prevCircleX;
    params.prevCircleY = prevCircleY;
    params.circleRadius = circleRadius;
    params.circleVelX = circleVelX;
    params.circleVelY = circleVelY;
    params.momentumTransferStrength = momentumTransferStrength;
    params.momentumTransferRadius = momentumTransferRadius;
    params.circleWasMoved = circleWasMoved;
    params.halfCellSize = cellSize * 0.5f;

    wgpuQueueWriteBuffer(queue, uniformBuffer, 0, &params, sizeof(SimParams));
}


// HISTOGRAM HELPER STRUCT
struct HistogramDispatchDesc {
    WGPUComputePipeline pipeline;
    WGPUBindGroup bindGroup;
    WGPUBuffer srcBuffer;
    WGPUBuffer stagingBuffer;
    size_t stagingBufferSize;
    const char* label;
    int slotIndex;
    void* simulator;
};

// COMPUTE SHADER DISPATCH HELPER
void GPUSimulator::dispatchComputePass(WGPUCommandEncoder encoder, WGPUComputePipeline pipeline, WGPUBindGroup bindGroup) {
    WGPUComputePassDescriptor computePassDesc = {};
    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);
    wgpuComputePassEncoderSetPipeline(computePass, pipeline);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);
    wgpuComputePassEncoderEnd(computePass);
    wgpuComputePassEncoderRelease(computePass);
}


// BOILERPLATE INSTANTIATION HELPERS
void GPUSimulator::createUniformBufferPipelineLayoutEntry(WGPUBindGroupLayoutEntry* entries) {
    entries[0] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = sizeof(SimParams);
    entries[0].buffer.hasDynamicOffset = false;
}

bool GPUSimulator::createTexture(const TextureDesc& desc, WGPUTexture& texture, WGPUTextureView& view) {
    texture = createTextureView(gridX, gridY, desc.format, desc.usage, view);
    return texture != nullptr;
}

void GPUSimulator::copyTextureDeviceToDevice(WGPUCommandEncoder encoder, WGPUTexture srcTexture, WGPUTexture dstTexture) {
    WGPUImageCopyTexture src = {};
    src.texture = srcTexture;
    src.mipLevel = 0;
    src.origin = {0, 0, 0};
    src.aspect = WGPUTextureAspect_All;

    WGPUImageCopyTexture dst = {};
    dst.texture = dstTexture;
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    WGPUExtent3D copySize = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);
}

WGPUComputePipeline GPUSimulator::createComputePipeline(const char* shaderFile, const char* entryPoint, WGPUPipelineLayout layout) {
    WGPUShaderModule shaderModule = createShaderModule(shaderFile);
    if (!shaderModule) {
        return nullptr;
    }

    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = layout;
    pipelineDesc.compute.module = shaderModule;
    pipelineDesc.compute.entryPoint = entryPoint;

    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    wgpuShaderModuleRelease(shaderModule);
    return pipeline;
}

WGPUBindGroupLayoutEntry GPUSimulator::createStorageTextureLayoutEntry(int binding, WGPUStorageTextureAccess access, WGPUTextureFormat format) {
    WGPUBindGroupLayoutEntry entry = {};
    entry.binding = binding;
    entry.visibility = WGPUShaderStage_Compute;
    entry.storageTexture.access = access;
    entry.storageTexture.format = format;
    entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
    return entry;
}

WGPUBindGroupLayoutEntry GPUSimulator::createStorageBufferLayoutEntry(int binding, size_t minSize) {
    WGPUBindGroupLayoutEntry entry = {};
    entry.binding = binding;
    entry.visibility = WGPUShaderStage_Compute;
    entry.buffer.type = WGPUBufferBindingType_Storage;
    entry.buffer.hasDynamicOffset = false;
    entry.buffer.minBindingSize = minSize;
    return entry;
}

WGPUBindGroupEntry GPUSimulator::createStorageBufferBindGroupEntry(int binding, WGPUBuffer buffer, size_t size) {
    WGPUBindGroupEntry entry = {};
    entry.binding = binding;
    entry.buffer = buffer;
    entry.offset = 0;
    entry.size = size;
    return entry;
}

// CONSTRUCTOR/DESTRUCTOR
GPUSimulator::GPUSimulator(const Config& config)
    : cpuSimulator(config)
{
    momentumTransferStrength = config.simulation.circle.momentumTransferStrength;
    momentumTransferRadius = config.simulation.circle.momentumTransferRadius;
}

GPUSimulator::~GPUSimulator() {
    RELEASE_PIPELINE_RESOURCES(integrate)
    RELEASE_PIPELINE_RESOURCES(divergence)
    RELEASE_PIPELINE_RESOURCES(jacobi)
    RELEASE_PIPELINE_RESOURCES(jacobiPingPong)
    RELEASE_PIPELINE_RESOURCES(velocityUpdate)
    RELEASE_PIPELINE_RESOURCES(extrapolate)
    RELEASE_PIPELINE_RESOURCES(advectVelocity)
    RELEASE_PIPELINE_RESOURCES(advectDensity)
    RELEASE_PIPELINE_RESOURCES(advectInk)
    RELEASE_PIPELINE_RESOURCES(boundary)
    RELEASE_PIPELINE_RESOURCES(vorticityCompute)
    RELEASE_PIPELINE_RESOURCES(vorticityApply)
    RELEASE_PIPELINE_RESOURCES(circle)
    RELEASE_PIPELINE_RESOURCES(pressureMinMax)
    RELEASE_PIPELINE_RESOURCES(histogramBins)

    // other histogram resources
    for (int i = 0; i < HISTOGRAM_RING_SIZE; i++) {
        auto& slot = histogramSlots[i];
        RELEASE_HISTOGRAM_SLOT_RESOURCES(slot);
    }

    RELEASE_TEXTURE(velocity)
    RELEASE_TEXTURE(pressure)
    RELEASE_TEXTURE(density)
    RELEASE_TEXTURE(solid)
    RELEASE_TEXTURE(ink)
    RELEASE_TEXTURE(divergence)
    RELEASE_TEXTURE(curl)
    RELEASE_TEXTURE(newVelocity)
    RELEASE_TEXTURE(newDensity)
    RELEASE_TEXTURE(newInk)
    RELEASE_TEXTURE(newPressure)

    RELEASE_BUFFER(uniformBuffer);
    RELEASE_SAMPLER(sampler);
}


// MAIN SIMULATION LOOP
bool GPUSimulator::init(const Config& cfg, const ImageData* imageData, float aspectRatio) {
    RETURN_FALSE_IF_FAIL(initSimData(cfg, imageData, aspectRatio));
    RETURN_FALSE_IF_FAIL(initTextures());
    sampler = createSampler(WGPUFilterMode_Linear);
    RETURN_FALSE_IF_FAIL(sampler);
    RETURN_FALSE_IF_FAIL(initUniformBuffer());
    RETURN_FALSE_IF_FAIL(initPipelineLayouts());
    RETURN_FALSE_IF_FAIL(initBindGroups());
    RETURN_FALSE_IF_FAIL(initPipelines());

    return true;
}

void GPUSimulator::copyInitialDataToGPU() {
    if (!device) return;

    // grab universal textures
    const auto& velX = cpuSimulator.getVelocityX();
    const auto& velY = cpuSimulator.getVelocityY();
    const auto& solid = cpuSimulator.getSolid();
    const auto& density = cpuSimulator.getDensity();

    // texture size is grid size
    WGPUExtent3D size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    
    // combine velocity data so we can write as single RG32Float texture
    std::vector<float> velocityData(gridX * gridY * 2);
    for (int j = 0; j < gridY; j++) {
        for (int i = 0; i < gridX; i++) {
            int idx = j * gridX + i;
            velocityData[idx * 2] = velX[idx];
            velocityData[idx * 2 + 1] = velY[idx];
        }
    }

    // write velocity data to both ping-pong textures
    WGPUImageCopyTexture copy = {};
    copy.mipLevel = 0;
    copy.origin = {0, 0, 0};
    copy.aspect = WGPUTextureAspect_All;

    WGPUTextureDataLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = gridX * 8; // 2 floats * 4 bytes
    layout.rowsPerImage = gridY;

    copy.texture = velocityTexture;
    wgpuQueueWriteTexture(queue, &copy, velocityData.data(), velocityData.size() * sizeof(float), &layout, &size);

    // write density data
    layout.bytesPerRow = gridX * 4; // 1 float * 4 bytes
    copy.texture = densityTexture;
    wgpuQueueWriteTexture(queue, &copy, density.data(), density.size() * sizeof(float), &layout, &size);
    
    // write solid data
    copy.texture = solidTexture;
    layout.bytesPerRow = gridX * 4; // 1 float * 4 bytes
    wgpuQueueWriteTexture(queue, &copy, solid.data(), solid.size() * sizeof(float), &layout, &size);

    // copy ink data if available
    if (inkInitialized) {
        const auto& redInk = cpuSimulator.getRedInk();
        const auto& greenInk = cpuSimulator.getGreenInk();
        const auto& blueInk = cpuSimulator.getBlueInk();

        // combine ink data so we can write as a single RGBA32Float texture
        std::vector<float> inkData(gridX * gridY * 4);
        for (int j = 0; j < gridY; j++) {
            for (int i = 0; i < gridX; i++) {
                int idx = j * gridX + i;
                inkData[idx * 4] = redInk[idx];
                inkData[idx * 4 + 1] = greenInk[idx];
                inkData[idx * 4 + 2] = blueInk[idx];
                inkData[idx * 4 + 3] = 1.0f; // alpha
            }
        }

        copy.texture = inkTexture;
        layout.bytesPerRow = gridX * 16; // 4 floats * 4 bytes
        wgpuQueueWriteTexture(queue, &copy, inkData.data(), inkData.size() * sizeof(float), &layout, &size);
    }

    updateUniformBufferSim();
}

void GPUSimulator::update() {
    updateUniformBufferSim();

    // by default, circle was not moved
    circleWasMoved = false;

    // encoder for the entire update
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

    // standard update loop
    if (gravity != 0.0f) {
        dispatchIntegrate(encoder);
    }
    dispatchCircle(encoder); // called every frame for pipeline consistency
    // ^ this is also possible because this is the only shader to not need access to other
    // pixels, so it is fully parallelizable with read-write and so just one compute pass is chill
    dispatchBoundaryConditions(encoder);
    dispatchProjection(encoder);
    dispatchExtrapolate(encoder);
    dispatchAdvect(encoder);
    if (config->simulation.vorticity.enabled) {
        dispatchVorticity(encoder);
    }

    // submit all commands at once
    WGPUCommandBufferDescriptor cmdDesc = {};
    cmdDesc.label = "Simulation Update";
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &commands);

    // find a free histogram slot and dispatch if available
    int freeSlot = -1;
    for (int i = 0; i < HISTOGRAM_RING_SIZE; i++) {
        int checkIdx = (histogramWriteIndex + i) % HISTOGRAM_RING_SIZE;
        if (histogramSlots[checkIdx].state == HistogramSlot_Free) {
            freeSlot = checkIdx;
            break;
        }
    }
    if (freeSlot >= 0) {
        auto& slot = histogramSlots[freeSlot];
        slot.state = HistogramSlot_Computing;
        histogramWriteIndex = (freeSlot + 1) % HISTOGRAM_RING_SIZE;
        dispatchPressureMinMax(freeSlot);
    }

    // clean up
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}


// MOUSE HELPERS
void GPUSimulator::moveCircle(int newGridX, int newGridY) {
    prevCircleX = circleX;
    prevCircleY = circleY;

    float timestep = config->simulation.timestep;
    float instantVelX = (newGridX - circleX) / timestep;
    float instantVelY = (newGridY - circleY) / timestep;

    float alpha = 0.3f; // smoothing factor
    circleVelX = alpha * instantVelX + (1.0f - alpha) * circleVelX;
    circleVelY = alpha * instantVelY + (1.0f - alpha) * circleVelY;

    circleX = newGridX;
    circleY = newGridY;

    // so this fixed a bug with laid over velocity from the momentum transfer
    // failing to dissipate and just spewing out from the ball after it stopped moving
    circleWasMoved = true;

    // the CPU version calls updateCircle here, but instead we call dispatchCircle() every frame
    // to make the pipeline consistent
}

// DISPATCH COMPUTE SHADERS
void GPUSimulator::dispatchIntegrate(WGPUCommandEncoder encoder) {
    dispatchComputePass(encoder, integratePipeline, integrateBindGroup);
    copyTextureDeviceToDevice(encoder, newVelocityTexture, velocityTexture);
}

void GPUSimulator::dispatchCircle(WGPUCommandEncoder encoder) {
    dispatchComputePass(encoder, circlePipeline, circleBindGroup);
    copyTextureDeviceToDevice(encoder, newVelocityTexture, velocityTexture);
}

void GPUSimulator::dispatchBoundaryConditions(WGPUCommandEncoder encoder) {
    dispatchComputePass(encoder, boundaryPipeline, boundaryBindGroup);
    copyTextureDeviceToDevice(encoder, newVelocityTexture, velocityTexture);
    copyTextureDeviceToDevice(encoder, newDensityTexture, densityTexture);
}

void GPUSimulator::dispatchProjection(WGPUCommandEncoder encoder) {
    // clear pressure textures each frame (CPU resets p to 0 every projection)
    {
        std::vector<float> zeroPressure(gridX * gridY, 0.0f);

        WGPUImageCopyTexture copy = {};
        copy.mipLevel = 0;
        copy.origin = {0, 0, 0};
        copy.aspect = WGPUTextureAspect_All;

        WGPUTextureDataLayout layout = {};
        layout.offset = 0;
        layout.bytesPerRow = gridX * sizeof(float);
        layout.rowsPerImage = gridY;

        WGPUExtent3D size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};

        copy.texture = pressureTexture;
        wgpuQueueWriteTexture(queue, &copy, zeroPressure.data(), zeroPressure.size() * sizeof(float), &layout, &size);

        copy.texture = newPressureTexture;
        wgpuQueueWriteTexture(queue, &copy, zeroPressure.data(), zeroPressure.size() * sizeof(float), &layout, &size);
    }

    // compute divergence
    dispatchComputePass(encoder, divergencePipeline, divergenceBindGroup);

    // run jacobi pressure solver with ping-pong
    int iterations = static_cast<int>(config->simulation.projection.iterations);
    for (int iter = 0; iter < iterations; iter++) {
        bool writeToPressure = (iter % 2 == 1);

        WGPUComputePassDescriptor computePassDesc = {};
        WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

        wgpuComputePassEncoderSetPipeline(computePass, jacobiPipeline);
        wgpuComputePassEncoderSetBindGroup(
            computePass,
            0,
            writeToPressure ? jacobiPingPongBindGroup : jacobiBindGroup,
            0,
            nullptr);

        wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

        wgpuComputePassEncoderEnd(computePass);
        wgpuComputePassEncoderRelease(computePass);
    }

    // make sure pressure data lives in the right texture
    if (iterations % 2 == 1) {
        copyTextureDeviceToDevice(encoder, newPressureTexture, pressureTexture);
    }

    // update velocity after running pressure solver
    dispatchComputePass(encoder, velocityUpdatePipeline, velocityUpdateBindGroup);
    copyTextureDeviceToDevice(encoder, newVelocityTexture, velocityTexture);
}

void GPUSimulator::dispatchExtrapolate(WGPUCommandEncoder encoder) {
    dispatchComputePass(encoder, extrapolatePipeline, extrapolateBindGroup);
    copyTextureDeviceToDevice(encoder, newVelocityTexture, velocityTexture);
}

void GPUSimulator::dispatchAdvect(WGPUCommandEncoder encoder) {
    dispatchComputePass(encoder, advectVelocityPipeline, advectVelocityBindGroup);
    dispatchComputePass(encoder, advectDensityPipeline, advectDensityBindGroup);
    dispatchComputePass(encoder, advectInkPipeline, advectInkBindGroup);
    copyTextureDeviceToDevice(encoder, newVelocityTexture, velocityTexture);
    copyTextureDeviceToDevice(encoder, newDensityTexture, densityTexture);
    copyTextureDeviceToDevice(encoder, newInkTexture, inkTexture);
}

void GPUSimulator::dispatchVorticity(WGPUCommandEncoder encoder) {
    dispatchComputePass(encoder, vorticityComputePipeline, vorticityComputeBindGroup);
    dispatchComputePass(encoder, vorticityApplyPipeline, vorticityApplyBindGroup);
    copyTextureDeviceToDevice(encoder, newVelocityTexture, velocityTexture);
}


// HISTOGRAM HELPERS
void GPUSimulator::dispatchHistogramCompute(const HistogramDispatchDesc& desc) {
    // separate encoder for histogram computation
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

    // compute pass
    WGPUComputePassDescriptor computePassDesc = {};
    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);
    wgpuComputePassEncoderSetPipeline(computePass, desc.pipeline);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, desc.bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);
    wgpuComputePassEncoderEnd(computePass);
    wgpuComputePassEncoderRelease(computePass);

    // copy to staging buffer for readback
    wgpuCommandEncoderCopyBufferToBuffer(encoder, desc.srcBuffer, 0, desc.stagingBuffer, 0, desc.stagingBufferSize);

    // submit commands
    WGPUCommandBufferDescriptor cmdDesc = {};
    cmdDesc.label = desc.label;
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

void GPUSimulator::dispatchPressureMinMax(int slotIndex) {
    auto& slot = histogramSlots[slotIndex];

    // reinitialize with +/- inf
    const float positiveInf = std::numeric_limits<float>::infinity();
    const float negativeInf = -std::numeric_limits<float>::infinity();
    uint32_t initialMinMax[4] = {
        floatToOrderedUint(positiveInf),
        floatToOrderedUint(negativeInf),
        floatToOrderedUint(positiveInf),
        floatToOrderedUint(negativeInf)
    };
    wgpuQueueWriteBuffer(queue, slot.minMaxBuffer, 0, initialMinMax, sizeof(initialMinMax));

    WGPUBindGroupEntry minMaxEntries[5] = {};
    minMaxEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    minMaxEntries[1] = createTextureViewBindGroupEntry(1, pressureTextureView);
    minMaxEntries[2] = createTextureViewBindGroupEntry(2, velocityTextureView);
    minMaxEntries[3] = createTextureViewBindGroupEntry(3, solidTextureView);
    minMaxEntries[4] = createStorageBufferBindGroupEntry(4, slot.minMaxBuffer, 4 * sizeof(uint32_t));

    if (slot.bindGroupMinMax) {
        wgpuBindGroupRelease(slot.bindGroupMinMax);
    }
    slot.bindGroupMinMax = createBindGroup(5, minMaxEntries, pressureMinMaxBindGroupLayout);

    // dispatch compute pass
    HistogramDispatchDesc desc = {};
    desc.pipeline = pressureMinMaxPipeline;
    desc.bindGroup = slot.bindGroupMinMax;
    desc.srcBuffer = slot.minMaxBuffer;
    desc.stagingBuffer = slot.minMaxStagingBuffer;
    desc.stagingBufferSize = 4 * sizeof(uint32_t);
    desc.label = "Histogram MinMax";
    desc.slotIndex = slotIndex;
    desc.simulator = this;
    dispatchHistogramCompute(desc);

    // map staging buffer asynchronously with callback
    WGPUBufferMapCallbackInfo2 mapCallbackInfo = {};
    mapCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    mapCallbackInfo.callback = [](WGPUMapAsyncStatus status, const char* message, void* userdata1, void* userdata2) {
        (void)message;
        GPUSimulator* sim = static_cast<GPUSimulator*>(userdata1);
        int slotIdx = static_cast<int>(reinterpret_cast<intptr_t>(userdata2));
        sim->onMinMaxMapped(status, slotIdx);
    };
    mapCallbackInfo.userdata1 = this;
    mapCallbackInfo.userdata2 = reinterpret_cast<void*>(static_cast<intptr_t>(slotIndex));
    wgpuBufferMapAsync2(slot.minMaxStagingBuffer, WGPUMapMode_Read, 0, 4 * sizeof(uint32_t), mapCallbackInfo);
}

void GPUSimulator::onMinMaxMapped(WGPUMapAsyncStatus status, int slotIndex) {
    auto& slot = histogramSlots[slotIndex];

    if (status == WGPUMapAsyncStatus_Success) {
        const uint32_t* data = static_cast<const uint32_t*>(
            wgpuBufferGetConstMappedRange(slot.minMaxStagingBuffer, 0, 4 * sizeof(uint32_t))
        );

        if (data) {
            slot.pendingPressureMinMax[0] = orderedUintToFloat(data[0]);  // pressMin
            slot.pendingPressureMinMax[1] = orderedUintToFloat(data[1]);  // pressMax
            slot.pendingVelocityMinMax[0] = orderedUintToFloat(data[2]);  // velMin
            slot.pendingVelocityMinMax[1] = orderedUintToFloat(data[3]);  // velMax

            std::cout << "GPU pressure min/max: min=" << slot.pendingPressureMinMax[0]
                      << ", max=" << slot.pendingPressureMinMax[1] << std::endl;
        }

        wgpuBufferUnmap(slot.minMaxStagingBuffer);

        // dispatch histogram bin counting
        slot.state = HistogramSlot_Binning;
        dispatchHistogramBins(slotIndex);
    } else {
        // mark slot as free so it can be retried
        slot.state = HistogramSlot_Free;
    }
}

void GPUSimulator::dispatchHistogramBins(int slotIndex) {
    auto& slot = histogramSlots[slotIndex];

    int32_t zeroBins[128] = {0};
    wgpuQueueWriteBuffer(queue, slot.histogramBinBuffer, 0, zeroBins, sizeof(zeroBins));

    // update min/max uniform buffer with unified min/max data from slot
    float minMaxUniform[4] = {
        slot.pendingPressureMinMax[0],
        slot.pendingPressureMinMax[1],
        slot.pendingVelocityMinMax[0],
        slot.pendingVelocityMinMax[1]
    };
    wgpuQueueWriteBuffer(queue, slot.minMaxUniformBuffer, 0, minMaxUniform, sizeof(minMaxUniform));

    WGPUBindGroupEntry histBinsEntries[6] = {};
    histBinsEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    histBinsEntries[1] = createUniformBufferBindGroupEntry(1, slot.minMaxUniformBuffer, 4 * sizeof(float));
    histBinsEntries[2] = createTextureViewBindGroupEntry(2, pressureTextureView);
    histBinsEntries[3] = createTextureViewBindGroupEntry(3, velocityTextureView);
    histBinsEntries[4] = createTextureViewBindGroupEntry(4, solidTextureView);
    histBinsEntries[5] = createStorageBufferBindGroupEntry(5, slot.histogramBinBuffer, 128 * sizeof(int32_t));

    if (slot.bindGroupHistogramBins) {
        wgpuBindGroupRelease(slot.bindGroupHistogramBins);
    }
    slot.bindGroupHistogramBins = createBindGroup(6, histBinsEntries, histogramBinsBindGroupLayout);

    // dispatch compute pass
    HistogramDispatchDesc desc = {};
    desc.pipeline = histogramBinsPipeline;
    desc.bindGroup = slot.bindGroupHistogramBins;
    desc.srcBuffer = slot.histogramBinBuffer;
    desc.stagingBuffer = slot.histogramStagingBuffer;
    desc.stagingBufferSize = 128 * sizeof(int32_t);
    desc.label = "Histogram Bins";
    desc.slotIndex = slotIndex;
    desc.simulator = this;
    dispatchHistogramCompute(desc);

    // map staging buffer for bins with callback
    WGPUBufferMapCallbackInfo2 binMapCallbackInfo = {};
    binMapCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    binMapCallbackInfo.callback = [](WGPUMapAsyncStatus status, const char* message, void* userdata1, void* userdata2) {
        (void)message;
        GPUSimulator* sim = static_cast<GPUSimulator*>(userdata1);
        int slotIdx = static_cast<int>(reinterpret_cast<intptr_t>(userdata2));
        sim->onHistogramBinsMapped(status, slotIdx);
    };
    binMapCallbackInfo.userdata1 = this;
    binMapCallbackInfo.userdata2 = reinterpret_cast<void*>(static_cast<intptr_t>(slotIndex));
    wgpuBufferMapAsync2(slot.histogramStagingBuffer, WGPUMapMode_Read, 0, 128 * sizeof(int32_t), binMapCallbackInfo);
}

void GPUSimulator::onHistogramBinsMapped(WGPUMapAsyncStatus status, int slotIndex) {
    auto& slot = histogramSlots[slotIndex];

    if (status == WGPUMapAsyncStatus_Success) {
        const int32_t* binData = static_cast<const int32_t*>(
            wgpuBufferGetConstMappedRange(slot.histogramStagingBuffer, 0, 128 * sizeof(int32_t))
        );

        if (binData) {
            // density bins
            for (int i = 0; i < 64; i++) {
                slot.pendingHistogramBins[i] = binData[i];
            }
            // velocity bins
            for (int i = 64; i < 128; i++) {
                slot.pendingHistogramBins[i] = binData[i];
            }
        }

        wgpuBufferUnmap(slot.histogramStagingBuffer);

        // mark slot as ready
        slot.state = HistogramSlot_Ready;
    } else {
        // mark slot as free so it can be retried
        slot.state = HistogramSlot_Free;
    }
}

bool GPUSimulator::getHistogramData(int& readySlot, const float*& pressureMinMax, const float*& velocityMinMax, const int*& histogramBins) const {
    // find the most recent ready slot
    for (int i = 0; i < HISTOGRAM_RING_SIZE; i++) {
        int checkIdx = (histogramReadIndex + i) % HISTOGRAM_RING_SIZE;
        if (histogramSlots[checkIdx].state == HistogramSlot_Ready) {
            readySlot = checkIdx;
            auto& slot = histogramSlots[checkIdx];
            pressureMinMax = slot.pendingPressureMinMax;
            velocityMinMax = slot.pendingVelocityMinMax;
            histogramBins = slot.pendingHistogramBins;
            return true;
        }
    }
    return false;
}

void GPUSimulator::advanceHistogramReadIndex() const {
    auto& slot = histogramSlots[histogramReadIndex];
    if (slot.state == HistogramSlot_Ready) {
        slot.state = HistogramSlot_Free;
        histogramReadIndex = (histogramReadIndex + 1) % HISTOGRAM_RING_SIZE;
    }
}


// GPU INITIALIZATION _BOILERPLATE_
bool GPUSimulator::initUniformBuffer() {
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.nextInChain = nullptr;
    uniformDesc.label = nullptr;
    uniformDesc.size = sizeof(SimParams);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformDesc.mappedAtCreation = false;

    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);
    RETURN_FALSE_IF_FAIL(uniformBuffer);
    return true;
}

bool GPUSimulator::initSimData(const Config& cfg, const ImageData* imageData, float aspectRatio) {
    this->config = &cfg;
    if (!cpuSimulator.init(cfg, imageData, aspectRatio)) {
        return false;
    }
    
    cellSize = cpuSimulator.cellSize;
    gridX = cpuSimulator.gridX;
    gridY = cpuSimulator.gridY;
    domainWidth = cpuSimulator.domainWidth;
    domainHeight = cpuSimulator.domainHeight;
    workgroupX = (gridX + 15) / 16;
    workgroupY = (gridY + 15) / 16;
    windTunnelStartCell = cpuSimulator.windTunnelStartCell;
    windTunnelEndCell = cpuSimulator.windTunnelEndCell;
    circleX = gridX / 2;
    circleY = gridY / 2;
    circleRadius = cpuSimulator.circleRadius;
    inkInitialized = cpuSimulator.inkInitialized;
    gravity = cpuSimulator.gravity;
    return true;
}

bool GPUSimulator::initTextures() {
    // RG32Float to store channels (x,y) together
    CREATE_STORAGE_TEXTURE(velocity, RG32Float);
    CREATE_STORAGE_TEXTURE(newVelocity, RG32Float);

    CREATE_STORAGE_TEXTURE(pressure, R32Float);
    CREATE_STORAGE_TEXTURE(newPressure, R32Float);

    CREATE_STORAGE_TEXTURE(density, R32Float);
    CREATE_STORAGE_TEXTURE(newDensity, R32Float);

    // stored as a float because WebGPU is shit
    CREATE_STORAGE_TEXTURE(solid, R32Float);

    // RGBA32Float to store all 3 channels (r,g,b) together
    CREATE_STORAGE_TEXTURE(ink, RGBA32Float);
    CREATE_STORAGE_TEXTURE(newInk, RGBA32Float);

    // these require their own textures because we compute all values in parallel
    // as opposed to more expensive texture sampling in the shaders that use these values
    CREATE_STORAGE_TEXTURE(divergence, R32Float);
    CREATE_STORAGE_TEXTURE(curl, R32Float);

    // initialize histogram ring buffer
    for (int i = 0; i < HISTOGRAM_RING_SIZE; i++) {
        auto& slot = histogramSlots[i];

        WGPUBufferDescriptor minMaxBufferDesc = {};
        minMaxBufferDesc.size = 4 * sizeof(uint32_t);
        minMaxBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        minMaxBufferDesc.mappedAtCreation = true;
        slot.minMaxBuffer = wgpuDeviceCreateBuffer(device, &minMaxBufferDesc);
        if (!slot.minMaxBuffer) return false;

        // initialize with +/- inf
        uint32_t* mappedData = static_cast<uint32_t*>(wgpuBufferGetMappedRange(slot.minMaxBuffer, 0, 4 * sizeof(uint32_t)));
        const float positiveInf = std::numeric_limits<float>::infinity();
        const float negativeInf = -std::numeric_limits<float>::infinity();
        mappedData[0] = floatToOrderedUint(positiveInf);
        mappedData[1] = floatToOrderedUint(negativeInf);
        mappedData[2] = floatToOrderedUint(positiveInf);
        mappedData[3] = floatToOrderedUint(negativeInf);
        wgpuBufferUnmap(slot.minMaxBuffer);

        slot.minMaxStagingBuffer = createBuffer(4 * sizeof(uint32_t), WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
        RETURN_FALSE_IF_FAIL(slot.minMaxStagingBuffer);

        slot.histogramBinBuffer = createBuffer(128 * sizeof(int32_t), WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
        RETURN_FALSE_IF_FAIL(slot.histogramBinBuffer);

        slot.histogramStagingBuffer = createBuffer(128 * sizeof(int32_t), WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
        RETURN_FALSE_IF_FAIL(slot.histogramStagingBuffer);

        slot.minMaxUniformBuffer = createBuffer(4 * sizeof(float), WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
        RETURN_FALSE_IF_FAIL(slot.minMaxUniformBuffer);

        // initialize state
        slot.state = HistogramSlot_Free;
        slot.bindGroupMinMax = nullptr;
        slot.bindGroupHistogramBins = nullptr;
        std::fill_n(slot.pendingHistogramBins, 128, 0);
        slot.pendingPressureMinMax[0] = 0.0f;
        slot.pendingPressureMinMax[1] = 0.0f;
        slot.pendingVelocityMinMax[0] = 0.0f;
        slot.pendingVelocityMinMax[1] = 0.0f;
    }

    return true;
}

bool GPUSimulator::initPipelineLayouts() {
    // integration [4]:
    // uniform
    // old velocity (read)
    // new velocity (write)
    // solid
    WGPUBindGroupLayoutEntry integrationEntries[4] = {};
    createUniformBufferPipelineLayoutEntry(integrationEntries);
    integrationEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    integrationEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RG32Float);
    integrationEntries[3] = createSampleTextureLayoutEntry(3, WGPUShaderStage_Compute);

    integrateBindGroupLayout = createBindGroupLayout(4, integrationEntries);
    RETURN_FALSE_IF_FAIL(integrateBindGroupLayout);
    integratePipelineLayout = createPipelineLayout(&integrateBindGroupLayout);
    RETURN_FALSE_IF_FAIL(integratePipelineLayout);

    // divergence [4]:
    // uniform
    // velocity (read)
    // divergence (write)
    // solid (read)
    WGPUBindGroupLayoutEntry divergenceEntries[4] = {};
    createUniformBufferPipelineLayoutEntry(divergenceEntries);
    divergenceEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    divergenceEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_R32Float);
    divergenceEntries[3] = createSampleTextureLayoutEntry(3, WGPUShaderStage_Compute);
    
    divergenceBindGroupLayout = createBindGroupLayout(4, divergenceEntries);
    RETURN_FALSE_IF_FAIL(divergenceBindGroupLayout);
    divergencePipelineLayout = createPipelineLayout(&divergenceBindGroupLayout);
    RETURN_FALSE_IF_FAIL(divergencePipelineLayout);

    // jacobi pressure solver [5]:
    // uniform
    // divergence (read)
    // pressure (read)
    // new pressure (write)
    // solid (read)
    WGPUBindGroupLayoutEntry jacobiEntries[5] = {};
    createUniformBufferPipelineLayoutEntry(jacobiEntries);
    jacobiEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);
    jacobiEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);
    jacobiEntries[3] = createStorageTextureLayoutEntry(3, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_R32Float);
    jacobiEntries[4] = createSampleTextureLayoutEntry(4, WGPUShaderStage_Compute);

    jacobiBindGroupLayout = createBindGroupLayout(5, jacobiEntries);
    RETURN_FALSE_IF_FAIL(jacobiBindGroupLayout);
    jacobiPipelineLayout = createPipelineLayout(&jacobiBindGroupLayout);
    RETURN_FALSE_IF_FAIL(jacobiPipelineLayout);

    // velocity update [5]:
    // uniform
    // velocity (read)
    // new velocity (write)
    // solid (read)
    // pressure (read)
    WGPUBindGroupLayoutEntry velocityUpdateEntries[5] = {};
    createUniformBufferPipelineLayoutEntry(velocityUpdateEntries);
    velocityUpdateEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    velocityUpdateEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RG32Float);
    velocityUpdateEntries[3] = createSampleTextureLayoutEntry(3, WGPUShaderStage_Compute);
    velocityUpdateEntries[4] = createStorageTextureLayoutEntry(4, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);

    velocityUpdateBindGroupLayout = createBindGroupLayout(5, velocityUpdateEntries);
    RETURN_FALSE_IF_FAIL(velocityUpdateBindGroupLayout);
    velocityUpdatePipelineLayout = createPipelineLayout(&velocityUpdateBindGroupLayout);
    RETURN_FALSE_IF_FAIL(velocityUpdatePipelineLayout);

    // extrapolate [3]:
    // uniform
    // velocity (read)
    // new velocity (write)
    WGPUBindGroupLayoutEntry extrapolateEntries[3] = {};
    createUniformBufferPipelineLayoutEntry(extrapolateEntries);
    extrapolateEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    extrapolateEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RG32Float);

    extrapolateBindGroupLayout = createBindGroupLayout(3, extrapolateEntries);
    RETURN_FALSE_IF_FAIL(extrapolateBindGroupLayout);
    extrapolatePipelineLayout = createPipelineLayout(&extrapolateBindGroupLayout);
    RETURN_FALSE_IF_FAIL(extrapolatePipelineLayout);

    // velocity advection [4]:
    // uniform
    // velocity (read)
    // solid (read)
    // new velocity (write)
    WGPUBindGroupLayoutEntry advectVelocityEntries[4] = {};
    createUniformBufferPipelineLayoutEntry(advectVelocityEntries);
    advectVelocityEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    advectVelocityEntries[2] = createSampleTextureLayoutEntry(2, WGPUShaderStage_Compute);
    advectVelocityEntries[3] = createStorageTextureLayoutEntry(3, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RG32Float);

    advectVelocityBindGroupLayout = createBindGroupLayout(4, advectVelocityEntries);
    RETURN_FALSE_IF_FAIL(advectVelocityBindGroupLayout);
    advectVelocityPipelineLayout = createPipelineLayout(&advectVelocityBindGroupLayout);
    RETURN_FALSE_IF_FAIL(advectVelocityPipelineLayout);

    // density advection [5]:
    // uniform
    // velocity (read)
    // density (read)
    // solid (read)
    // new density (write)
    WGPUBindGroupLayoutEntry advectDensityEntries[5] = {};
    createUniformBufferPipelineLayoutEntry(advectDensityEntries);
    advectDensityEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    advectDensityEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);
    advectDensityEntries[3] = createSampleTextureLayoutEntry(3, WGPUShaderStage_Compute);
    advectDensityEntries[4] = createStorageTextureLayoutEntry(4, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_R32Float);

    advectDensityBindGroupLayout = createBindGroupLayout(5, advectDensityEntries);
    RETURN_FALSE_IF_FAIL(advectDensityBindGroupLayout);
    advectDensityPipelineLayout = createPipelineLayout(&advectDensityBindGroupLayout);
    RETURN_FALSE_IF_FAIL(advectDensityPipelineLayout);

    // ink advection [5]:
    // uniform
    // velocity (read)
    // ink (read)
    // solid (read)
    // new ink (write)
    WGPUBindGroupLayoutEntry advectInkEntries[5] = {};
    createUniformBufferPipelineLayoutEntry(advectInkEntries);
    advectInkEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    advectInkEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RGBA32Float);
    advectInkEntries[3] = createSampleTextureLayoutEntry(3, WGPUShaderStage_Compute);
    advectInkEntries[4] = createStorageTextureLayoutEntry(4, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RGBA32Float);

    advectInkBindGroupLayout = createBindGroupLayout(5, advectInkEntries);
    RETURN_FALSE_IF_FAIL(advectInkBindGroupLayout);
    advectInkPipelineLayout = createPipelineLayout(&advectInkBindGroupLayout);
    RETURN_FALSE_IF_FAIL(advectInkPipelineLayout);

    // boundary condition enforcement [6]:
    // uniform
    // velocity (read)
    // solid (read)
    // new velocity (write)
    // density (read)
    // new density (write)
    WGPUBindGroupLayoutEntry boundaryEntries[6] = {};
    createUniformBufferPipelineLayoutEntry(boundaryEntries);
    boundaryEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    boundaryEntries[2] = createSampleTextureLayoutEntry(2, WGPUShaderStage_Compute);
    boundaryEntries[3] = createStorageTextureLayoutEntry(3, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RG32Float);
    boundaryEntries[4] = createStorageTextureLayoutEntry(4, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);
    boundaryEntries[5] = createStorageTextureLayoutEntry(5, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_R32Float);

    boundaryBindGroupLayout = createBindGroupLayout(6, boundaryEntries);
    RETURN_FALSE_IF_FAIL(boundaryBindGroupLayout);
    boundaryPipelineLayout = createPipelineLayout(&boundaryBindGroupLayout);
    RETURN_FALSE_IF_FAIL(boundaryPipelineLayout);

    // vorticity computation [4]:
    // uniform
    // velocity (read)
    // solid (read)
    // curl (write)
    WGPUBindGroupLayoutEntry vorticityComputeEntries[4] = {};
    createUniformBufferPipelineLayoutEntry(vorticityComputeEntries);
    vorticityComputeEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    vorticityComputeEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);
    vorticityComputeEntries[3] = createStorageTextureLayoutEntry(3, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_R32Float);

    vorticityComputeBindGroupLayout = createBindGroupLayout(4, vorticityComputeEntries);
    RETURN_FALSE_IF_FAIL(vorticityComputeBindGroupLayout);
    vorticityComputePipelineLayout = createPipelineLayout(&vorticityComputeBindGroupLayout);
    RETURN_FALSE_IF_FAIL(vorticityComputePipelineLayout);

    // vorticity apply [5]:
    // uniform
    // velocity (read)
    // new velocity (write)
    // solid (read)
    // curl (read)
    WGPUBindGroupLayoutEntry vorticityApplyEntries[5] = {};
    createUniformBufferPipelineLayoutEntry(vorticityApplyEntries);
    vorticityApplyEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    vorticityApplyEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RG32Float);
    vorticityApplyEntries[3] = createStorageTextureLayoutEntry(3, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);
    vorticityApplyEntries[4] = createStorageTextureLayoutEntry(4, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);

    vorticityApplyBindGroupLayout = createBindGroupLayout(5, vorticityApplyEntries);
    RETURN_FALSE_IF_FAIL(vorticityApplyBindGroupLayout);
    vorticityApplyPipelineLayout = createPipelineLayout(&vorticityApplyBindGroupLayout);
    RETURN_FALSE_IF_FAIL(vorticityApplyPipelineLayout);

    // circle update [5]:
    // uniform
    // solid (read-write)
    // velocity (read)
    // new velocity (write)
    // density (read-write)
    WGPUBindGroupLayoutEntry circleLayoutEntries[5] = {};
    createUniformBufferPipelineLayoutEntry(circleLayoutEntries);
    circleLayoutEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadWrite, WGPUTextureFormat_R32Float);
    circleLayoutEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    circleLayoutEntries[3] = createStorageTextureLayoutEntry(3, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RG32Float);
    circleLayoutEntries[4] = createStorageTextureLayoutEntry(4, WGPUStorageTextureAccess_ReadWrite, WGPUTextureFormat_R32Float);

    circleBindGroupLayout = createBindGroupLayout(5, circleLayoutEntries);
    RETURN_FALSE_IF_FAIL(circleBindGroupLayout);
    circlePipelineLayout = createPipelineLayout(&circleBindGroupLayout);
    RETURN_FALSE_IF_FAIL(circlePipelineLayout);

    // pressure minmax [5]:
    // uniform
    // pressure (read)
    // velocity (read)
    // solid (read)
    // minmax storage buffer
    WGPUBindGroupLayoutEntry pressureMinMaxEntries[5] = {};
    createUniformBufferPipelineLayoutEntry(pressureMinMaxEntries);
    pressureMinMaxEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);
    pressureMinMaxEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    pressureMinMaxEntries[3] = createStorageTextureLayoutEntry(3, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);
    pressureMinMaxEntries[4] = createStorageBufferLayoutEntry(4, 4 * sizeof(uint32_t));

    pressureMinMaxBindGroupLayout = createBindGroupLayout(5, pressureMinMaxEntries);
    RETURN_FALSE_IF_FAIL(pressureMinMaxBindGroupLayout);
    pressureMinMaxPipelineLayout = createPipelineLayout(&pressureMinMaxBindGroupLayout);
    RETURN_FALSE_IF_FAIL(pressureMinMaxPipelineLayout);

    // histogram bins [6]:
    // uniform
    // minmax uniform
    // pressure (read)
    // velocity (read)
    // solid (read)
    // histogram bins storage buffer
    WGPUBindGroupLayoutEntry histogramBinsEntries[6] = {};
    createUniformBufferPipelineLayoutEntry(histogramBinsEntries);
    histogramBinsEntries[1] = createUniformBufferLayoutEntry(1, 4 * sizeof(float));
    histogramBinsEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);
    histogramBinsEntries[3] = createStorageTextureLayoutEntry(3, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    histogramBinsEntries[4] = createStorageTextureLayoutEntry(4, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);
    histogramBinsEntries[5] = createStorageBufferLayoutEntry(5, 128 * sizeof(int32_t));

    histogramBinsBindGroupLayout = createBindGroupLayout(6, histogramBinsEntries);
    RETURN_FALSE_IF_FAIL(histogramBinsBindGroupLayout);
    histogramBinsPipelineLayout = createPipelineLayout(&histogramBinsBindGroupLayout);
    RETURN_FALSE_IF_FAIL(histogramBinsPipelineLayout);

    return true;
}

bool GPUSimulator::initBindGroups() {
    // integrate [4]
    WGPUBindGroupEntry integrateEntries[4] = {};
    integrateEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    integrateEntries[1] = createTextureViewBindGroupEntry(1, velocityTextureView);
    integrateEntries[2] = createTextureViewBindGroupEntry(2, newVelocityTextureView);
    integrateEntries[3] = createTextureViewBindGroupEntry(3, solidTextureView);
    
    integrateBindGroup = createBindGroup(4, integrateEntries, integrateBindGroupLayout);
    RETURN_FALSE_IF_FAIL(integrateBindGroup);

    // divergence [4]
    WGPUBindGroupEntry divergenceEntries[4] = {};
    divergenceEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    divergenceEntries[1] = createTextureViewBindGroupEntry(1, velocityTextureView);
    divergenceEntries[2] = createTextureViewBindGroupEntry(2, divergenceTextureView);
    divergenceEntries[3] = createTextureViewBindGroupEntry(3, solidTextureView);

    divergenceBindGroup = createBindGroup(4, divergenceEntries, divergenceBindGroupLayout);
    RETURN_FALSE_IF_FAIL(divergenceBindGroup);

    // jacobi [5]
    WGPUBindGroupEntry jacobiEntries[5] = {};
    jacobiEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    jacobiEntries[1] = createTextureViewBindGroupEntry(1, divergenceTextureView);
    jacobiEntries[2] = createTextureViewBindGroupEntry(2, pressureTextureView);
    jacobiEntries[3] = createTextureViewBindGroupEntry(3, newPressureTextureView);
    jacobiEntries[4] = createTextureViewBindGroupEntry(4, solidTextureView);

    jacobiBindGroup = createBindGroup(5, jacobiEntries, jacobiBindGroupLayout);
    RETURN_FALSE_IF_FAIL(jacobiBindGroup);

    // jacobi ping-pong [5]
    WGPUBindGroupEntry jacobiPingPongEntries[5] = {};
    jacobiPingPongEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    jacobiPingPongEntries[1] = createTextureViewBindGroupEntry(1, divergenceTextureView);
    jacobiPingPongEntries[2] = createTextureViewBindGroupEntry(2, newPressureTextureView);
    jacobiPingPongEntries[3] = createTextureViewBindGroupEntry(3, pressureTextureView);
    jacobiPingPongEntries[4] = createTextureViewBindGroupEntry(4, solidTextureView);

    jacobiPingPongBindGroup = createBindGroup(5, jacobiPingPongEntries, jacobiBindGroupLayout);
    RETURN_FALSE_IF_FAIL(jacobiPingPongBindGroup);

    // velocity update [5]
    WGPUBindGroupEntry velocityUpdateEntries[5] = {};
    velocityUpdateEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    velocityUpdateEntries[1] = createTextureViewBindGroupEntry(1, velocityTextureView);
    velocityUpdateEntries[2] = createTextureViewBindGroupEntry(2, newVelocityTextureView);
    velocityUpdateEntries[3] = createTextureViewBindGroupEntry(3, solidTextureView);
    velocityUpdateEntries[4] = createTextureViewBindGroupEntry(4, pressureTextureView);

    velocityUpdateBindGroup = createBindGroup(5, velocityUpdateEntries, velocityUpdateBindGroupLayout);
    RETURN_FALSE_IF_FAIL(velocityUpdateBindGroup);

    // extrapolate [3]
    WGPUBindGroupEntry extrapolateBindEntries[3] = {};
    extrapolateBindEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    extrapolateBindEntries[1] = createTextureViewBindGroupEntry(1, velocityTextureView);
    extrapolateBindEntries[2] = createTextureViewBindGroupEntry(2, newVelocityTextureView);

    extrapolateBindGroup = createBindGroup(3, extrapolateBindEntries, extrapolateBindGroupLayout);
    RETURN_FALSE_IF_FAIL(extrapolateBindGroup);

    // advect velocity [4]
    WGPUBindGroupEntry advectVelocityEntries[4] = {};
    advectVelocityEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    advectVelocityEntries[1] = createTextureViewBindGroupEntry(1, velocityTextureView);
    advectVelocityEntries[2] = createTextureViewBindGroupEntry(2, solidTextureView);
    advectVelocityEntries[3] = createTextureViewBindGroupEntry(3, newVelocityTextureView);

    advectVelocityBindGroup = createBindGroup(4, advectVelocityEntries, advectVelocityBindGroupLayout);
    RETURN_FALSE_IF_FAIL(advectVelocityBindGroup);

    // advect density [5]
    WGPUBindGroupEntry advectDensityEntries[5] = {};
    advectDensityEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    advectDensityEntries[1] = createTextureViewBindGroupEntry(1, velocityTextureView);
    advectDensityEntries[2] = createTextureViewBindGroupEntry(2, densityTextureView);
    advectDensityEntries[3] = createTextureViewBindGroupEntry(3, solidTextureView);
    advectDensityEntries[4] = createTextureViewBindGroupEntry(4, newDensityTextureView);

    advectDensityBindGroup = createBindGroup(5, advectDensityEntries, advectDensityBindGroupLayout);
    RETURN_FALSE_IF_FAIL(advectDensityBindGroup);

    // advect ink [5]
    WGPUBindGroupEntry advectInkEntries[5] = {};
    advectInkEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    advectInkEntries[1] = createTextureViewBindGroupEntry(1, velocityTextureView);
    advectInkEntries[2] = createTextureViewBindGroupEntry(2, inkTextureView);
    advectInkEntries[3] = createTextureViewBindGroupEntry(3, solidTextureView);
    advectInkEntries[4] = createTextureViewBindGroupEntry(4, newInkTextureView);

    advectInkBindGroup = createBindGroup(5, advectInkEntries, advectInkBindGroupLayout);
    RETURN_FALSE_IF_FAIL(advectInkBindGroup);

    // boundary [6]
    WGPUBindGroupEntry boundaryEntries[6] = {};
    boundaryEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    boundaryEntries[1] = createTextureViewBindGroupEntry(1, velocityTextureView);
    boundaryEntries[2] = createTextureViewBindGroupEntry(2, solidTextureView);
    boundaryEntries[3] = createTextureViewBindGroupEntry(3, newVelocityTextureView);
    boundaryEntries[4] = createTextureViewBindGroupEntry(4, densityTextureView);
    boundaryEntries[5] = createTextureViewBindGroupEntry(5, newDensityTextureView);

    boundaryBindGroup = createBindGroup(6, boundaryEntries, boundaryBindGroupLayout);
    RETURN_FALSE_IF_FAIL(boundaryBindGroup);

    // vorticity compute [4]
    WGPUBindGroupEntry vorticityComputeEntries[4] = {};
    vorticityComputeEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    vorticityComputeEntries[1] = createTextureViewBindGroupEntry(1, velocityTextureView);
    vorticityComputeEntries[2] = createTextureViewBindGroupEntry(2, solidTextureView);
    vorticityComputeEntries[3] = createTextureViewBindGroupEntry(3, curlTextureView);

    vorticityComputeBindGroup = createBindGroup(4, vorticityComputeEntries, vorticityComputeBindGroupLayout);
    RETURN_FALSE_IF_FAIL(vorticityComputeBindGroup);

    // vorticity apply [5]
    WGPUBindGroupEntry vorticityApplyEntries[5] = {};
    vorticityApplyEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    vorticityApplyEntries[1] = createTextureViewBindGroupEntry(1, velocityTextureView);
    vorticityApplyEntries[2] = createTextureViewBindGroupEntry(2, newVelocityTextureView);
    vorticityApplyEntries[3] = createTextureViewBindGroupEntry(3, solidTextureView);
    vorticityApplyEntries[4] = createTextureViewBindGroupEntry(4, curlTextureView);

    vorticityApplyBindGroup = createBindGroup(5, vorticityApplyEntries, vorticityApplyBindGroupLayout);
    RETURN_FALSE_IF_FAIL(vorticityApplyBindGroup);

    // circle update [5]
    WGPUBindGroupEntry circleEntries[5] = {};
    circleEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    circleEntries[1] = createTextureViewBindGroupEntry(1, solidTextureView);
    circleEntries[2] = createTextureViewBindGroupEntry(2, velocityTextureView);
    circleEntries[3] = createTextureViewBindGroupEntry(3, newVelocityTextureView);
    circleEntries[4] = createTextureViewBindGroupEntry(4, densityTextureView);

    circleBindGroup = createBindGroup(5, circleEntries, circleBindGroupLayout);
    RETURN_FALSE_IF_FAIL(circleBindGroup);

    // histogram bind groups are created dynamically per-slot in dispatch methods

    return true;
}

bool GPUSimulator::initPipelines() {
    // create compute pipelines
    integratePipeline = createComputePipeline("compute_integrate.wgsl", "integrate", integratePipelineLayout);
    RETURN_FALSE_IF_FAIL(integratePipeline);

    divergencePipeline = createComputePipeline("compute_divergence.wgsl", "divergence", divergencePipelineLayout);
    RETURN_FALSE_IF_FAIL(divergencePipeline);
    jacobiPipeline = createComputePipeline("compute_jacobi.wgsl", "jacobiIteration", jacobiPipelineLayout);
    RETURN_FALSE_IF_FAIL(jacobiPipeline);
    velocityUpdatePipeline = createComputePipeline("compute_jacobi_apply.wgsl", "applyProjection", velocityUpdatePipelineLayout);
    RETURN_FALSE_IF_FAIL(velocityUpdatePipeline);

    extrapolatePipeline = createComputePipeline("compute_extrapolate.wgsl", "extrapolate", extrapolatePipelineLayout);
    RETURN_FALSE_IF_FAIL(extrapolatePipeline);

    advectVelocityPipeline = createComputePipeline("compute_advect_velocity.wgsl", "advectVelocity", advectVelocityPipelineLayout);
    RETURN_FALSE_IF_FAIL(advectVelocityPipeline);
    advectDensityPipeline = createComputePipeline("compute_advect_density.wgsl", "advectDensity", advectDensityPipelineLayout);
    RETURN_FALSE_IF_FAIL(advectDensityPipeline);
    advectInkPipeline = createComputePipeline("compute_advect_ink.wgsl", "advectInk", advectInkPipelineLayout);
    RETURN_FALSE_IF_FAIL(advectInkPipeline);

    boundaryPipeline = createComputePipeline("compute_boundary.wgsl", "enforceBoundaryConditions", boundaryPipelineLayout);
    RETURN_FALSE_IF_FAIL(boundaryPipeline);

    vorticityComputePipeline = createComputePipeline("compute_curl.wgsl", "curl", vorticityComputePipelineLayout);
    RETURN_FALSE_IF_FAIL(vorticityComputePipeline);

    vorticityApplyPipeline = createComputePipeline("compute_vorticity.wgsl", "vorticity", vorticityApplyPipelineLayout);
    RETURN_FALSE_IF_FAIL(vorticityApplyPipeline);

    circlePipeline = createComputePipeline("compute_circle.wgsl", "updateCircle", circlePipelineLayout);
    RETURN_FALSE_IF_FAIL(circlePipeline);

    pressureMinMaxPipeline = createComputePipeline("compute_pressure_minmax.wgsl", "computePressureMinMax", pressureMinMaxPipelineLayout);
    RETURN_FALSE_IF_FAIL(pressureMinMaxPipeline);

    histogramBinsPipeline = createComputePipeline("compute_histogram_bins.wgsl", "computeHistogramBins", histogramBinsPipelineLayout);
    RETURN_FALSE_IF_FAIL(histogramBinsPipeline);

    copyInitialDataToGPU(); // non-boilerplate, so separate function
    return true;
}
