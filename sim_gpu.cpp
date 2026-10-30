#include "boilerplate.h"
#include "sim_gpu.h"
#include <iostream>
#include <cstring>
#include <fstream>
#include <vector>
#include <limits>
#include <cmath>
#include <algorithm>

// CONSTRUCTOR :150

// UPDATE SIM PARAMS UNIFORM
void GPUSimulator::updateUniformBufferSim() {
    SimParams params = {};
    params.gridX = gridX;
    params.gridY = gridY;
    params.overrelaxationCoefficient = config->simulation.projection.overrelaxationCoefficient;
    params.timestep = config->simulation.timestep;
    params.cellSize = cellSize;
    params.halfCellSize = cellSize * 0.5f;
    params.windTunnelSide = config->simulation.windTunnel.side;
    params.windTunnelStart = windTunnelStartCell;
    params.windTunnelEnd = windTunnelEndCell;
    params.windTunnelSpeed = config->simulation.windTunnel.velocity;
    params.edges = config->simulation.edges;
    params.vorticity = config->simulation.vorticity.strength;
    params.vorticityLen = config->simulation.vorticity.lengthScale;
    params.inputMode = inputModeToInt(g_config.inputMode);

    int gpuCircleCount = 0;
    int gpuSegmentCount = 0;
    uploadInputBuffers(gpuCircleCount, gpuSegmentCount);
    params.numCircles = gpuCircleCount;
    params.numSegments = gpuSegmentCount;
    params.numPresentSegments = gpuSegmentCount;

    params.momentumTransferStrength = momentumTransferStrength;
    params.momentumTransferRadius = momentumTransferRadius;
    const HandSensitivityParams handSensitivity = resolveHandSensitivity(config->simulation.circle);
    params.momentumTransferDeadZone = handSensitivity.momentumDeadZone;
    params.momentumLowMotionScale = handSensitivity.lowMotionImpulseScale;
    params.momentumLowMotionSoftCeilingMul = handSensitivity.lowMotionSoftCeilingMul;
    params.clearDensityOnVacate = config->simulation.circle.clearDensityOnVacate ? 1 : 0;

    wgpuQueueWriteBuffer(queue, uniformBuffer, 0, &params, sizeof(SimParams));
}

// Packs only the entries the input shader can act on: circles that are present or
// were present last frame, and segments that are present. Compacting here keeps the
// per-cell shader loops proportional to what is actually on screen (one hand = ~half).
void GPUSimulator::uploadInputBuffers(int& circleCount, int& segmentCount) {
    circleCount = 0;
    segmentCount = 0;

    if (!circleBuffer || !segmentBuffer) {
        return;
    }

    CircleGPU gpuCircles[HandTracking::MAX_CIRCLES];
    SegmentGPU gpuSegments[HandTracking::MAX_SEGMENTS];

    if (isMouseInput(g_config.inputMode)) {
        if (isMouseDragging) {
            CircleGPU& dst = gpuCircles[circleCount++];
            dst.posX = static_cast<float>(mouseCircleX);
            dst.posY = static_cast<float>(mouseCircleY);
            dst.prevPosX = static_cast<float>(mousePrevCircleX);
            dst.prevPosY = static_cast<float>(mousePrevCircleY);
            dst.velX = mouseCircleVelX;
            dst.velY = mouseCircleVelY;
            dst.radius = static_cast<float>(mouseCircleRadius);
            dst.flags = CIRCLE_FLAG_PRESENT | CIRCLE_FLAG_WAS_PRESENT;
        }
    } else {
        for (int i = 0; i < numCircles && i < HandTracking::MAX_CIRCLES; i++) {
            const CircleState& src = circles[i];
            if (!src.present && !src.wasPresent) {
                continue;
            }

            CircleGPU& dst = gpuCircles[circleCount++];
            dst.posX = static_cast<float>(src.x);
            dst.posY = static_cast<float>(src.y);
            dst.prevPosX = static_cast<float>(src.prevX);
            dst.prevPosY = static_cast<float>(src.prevY);
            dst.velX = src.velX;
            dst.velY = src.velY;
            dst.radius = static_cast<float>(src.scaledRadius);
            dst.flags = (src.present ? CIRCLE_FLAG_PRESENT : 0u) |
                        (src.wasPresent ? CIRCLE_FLAG_WAS_PRESENT : 0u);
        }

        for (int i = 0; i < numSegments && i < HandTracking::MAX_SEGMENTS; i++) {
            const LineSegment& src = segments[i];
            if (!src.present) {
                continue;
            }

            SegmentGPU& dst = gpuSegments[segmentCount++];
            dst.startX = static_cast<float>(src.startX);
            dst.startY = static_cast<float>(src.startY);
            dst.endX = static_cast<float>(src.endX);
            dst.endY = static_cast<float>(src.endY);
            dst.startVelX = src.startVelX;
            dst.startVelY = src.startVelY;
            dst.endVelX = src.endVelX;
            dst.endVelY = src.endVelY;
            dst.startRadius = src.startRadius;
            dst.endRadius = src.endRadius;

            // conservative bounding circle around the capsule, for the shader's early reject
            const float axisX = dst.endX - dst.startX;
            const float axisY = dst.endY - dst.startY;
            const float halfLength = 0.5f * std::sqrt(axisX * axisX + axisY * axisY);
            const float cullRadius = halfLength + std::max(dst.startRadius, dst.endRadius);
            dst.cullRadiusSq = cullRadius * cullRadius;
            dst._pad = 0.0f;
        }
    }

    if (circleCount > 0) {
        wgpuQueueWriteBuffer(queue, circleBuffer, 0, gpuCircles, circleCount * sizeof(CircleGPU));
    }
    if (segmentCount > 0) {
        wgpuQueueWriteBuffer(queue, segmentBuffer, 0, gpuSegments, segmentCount * sizeof(SegmentGPU));
    }
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
    pipelineDesc.compute.entryPoint = WGPU_CSTR(entryPoint);

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

WGPUBindGroupLayoutEntry GPUSimulator::createStorageBufferLayoutEntry(int binding, size_t minSize, bool readOnly) {
    WGPUBindGroupLayoutEntry entry = {};
    entry.binding = binding;
    entry.visibility = WGPUShaderStage_Compute;
    entry.buffer.type = readOnly ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
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

    baseCircleRadius = static_cast<int>(config.simulation.circle.radius / cpuSimulator.cellSize);
    for (int i = 0; i < HandTracking::MAX_CIRCLES; i++) {
        circles[i].x = 0;
        circles[i].y = 0;
        circles[i].prevX = 0;
        circles[i].prevY = 0;
        circles[i].z = 0.0f;
        circles[i].scaledRadius = 0;
        circles[i].present = false;
        circles[i].wasPresent = false;
    }
}

GPUSimulator::~GPUSimulator() {
    RELEASE_PIPELINE_RESOURCES(divergence)
    RELEASE_PIPELINE_RESOURCES(jacobi)
    RELEASE_PIPELINE_RESOURCES(jacobiPingPong)
    RELEASE_PIPELINE_RESOURCES(gs)
    releaseResource(gsBlackPipeline, wgpuComputePipelineRelease);
    RELEASE_PIPELINE_RESOURCES(velocityUpdate)
    RELEASE_PIPELINE_RESOURCES(extrapolate)
    RELEASE_PIPELINE_RESOURCES(advectVelocity)
    RELEASE_PIPELINE_RESOURCES(advectDensity)
    RELEASE_PIPELINE_RESOURCES(advectInk)
    RELEASE_PIPELINE_RESOURCES(boundary)
    RELEASE_PIPELINE_RESOURCES(vorticityCompute)
    RELEASE_PIPELINE_RESOURCES(vorticityApply)
    RELEASE_PIPELINE_RESOURCES(input)
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
    RELEASE_TEXTURE(newSolid)

    RELEASE_BUFFER(uniformBuffer);
    RELEASE_BUFFER(circleBuffer);
    RELEASE_BUFFER(segmentBuffer);
    RELEASE_SAMPLER(sampler);
}


// MAIN SIMULATION LOOP
bool GPUSimulator::init(const Config& cfg, const ImageData* imageData, float aspectRatio) {
    RETURN_FALSE_IF_FAIL(initSimData(cfg, imageData, aspectRatio));
    RETURN_FALSE_IF_FAIL(rebuildGridResources());
    if (histogramSlots[0].minMaxBuffer == nullptr) {
        RETURN_FALSE_IF_FAIL(initHistogramResources());
    }

    if (divergencePipeline != nullptr) {
        return true;
    }

    sampler = createSampler(WGPUFilterMode_Linear);
    RETURN_FALSE_IF_FAIL(sampler);

    RETURN_FALSE_IF_FAIL(initUniformBuffer());
    RETURN_FALSE_IF_FAIL(initPipelineLayouts());
    RETURN_FALSE_IF_FAIL(initBindGroups());
    RETURN_FALSE_IF_FAIL(initPipelines());
    copyInitialDataToGPU();

    return true;
}

void GPUSimulator::copyInitialDataToGPU() {
    if (!device || !uniformBuffer) return;

    uploadScalarGpuFieldsFromCpu();

    WGPUExtent3D size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    WGPUImageCopyTexture copy = {};
    copy.mipLevel = 0;
    copy.origin = {0, 0, 0};
    copy.aspect = WGPUTextureAspect_All;

    WGPUTextureDataLayout layout = {};
    layout.offset = 0;
    layout.rowsPerImage = gridY;

    if (inkInitialized) {
        const auto& inkData = cpuSimulator.getInk();
        copy.texture = inkTexture;
        layout.bytesPerRow = gridX * 16;
        wgpuQueueWriteTexture(queue, &copy, inkData.data(), inkData.size() * sizeof(float), &layout, &size);
        copy.texture = newInkTexture;
        wgpuQueueWriteTexture(queue, &copy, inkData.data(), inkData.size() * sizeof(float), &layout, &size);
    }

    updateUniformBufferSim();
}

void GPUSimulator::update() {
    updateUniformBufferSim();

    // encoder for the entire update
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

    // one pass handles skeleton segments and joint circles together
    if (isMouseInput(g_config.inputMode)) {
        if (isMouseDragging) {
            dispatchInput(encoder);
            mousePrevCircleX = mouseCircleX;
            mousePrevCircleY = mouseCircleY;
        }
    } else {
        dispatchInput(encoder);
        for (int i = 0; i < numCircles; i++) {
            circles[i].prevX = circles[i].x;
            circles[i].prevY = circles[i].y;
            if (!circles[i].present) {
                circles[i].wasPresent = false;
            }
        }
    }
    // ^ fun fact, this is the only shader to not need access to other pixels (e.g. neighbors),
    // so it is fully parallelizable with read-write and so just one compute pass is chill

    // Match CPU: sync boundaries before divergence/projection.
    dispatchBoundaryConditions(encoder);

    dispatchProjection(encoder);
    dispatchExtrapolate(encoder);
    dispatchAdvect(encoder);
    if (config->simulation.vorticity.strength > 0.0f) {
        dispatchVorticity(encoder);
    }
    dispatchBoundaryConditions(encoder);

    // submit all commands at once
    WGPUCommandBufferDescriptor cmdDesc = {};
    cmdDesc.label = WGPU_CSTR("Simulation Update");
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
    // Keep mousePrev* from the last GPU frame so dispatchInput can clear the
    // previous solid footprint even when multiple drags land before the next update.

    float instantVelX = (newGridX - mouseCircleX) / config->simulation.timestep;
    float instantVelY = (newGridY - mouseCircleY) / config->simulation.timestep;
    mouseCircleVelX = instantVelX;
    mouseCircleVelY = instantVelY;

    mouseCircleX = newGridX;
    mouseCircleY = newGridY;

    // the CPU version calls updateCircle here, but instead we call dispatchInput() every frame
    // to make the pipeline consistent
}

int GPUSimulator::scaleRadiusByZ(float z, int baseRadius) {
    return ::scaleRadiusByZ(z, baseRadius, config->simulation.circle);
}

void GPUSimulator::updateCircles(const FingertipData* fingertips, int count) {
    int updateCount = count < HandTracking::MAX_CIRCLES ? count : HandTracking::MAX_CIRCLES;
    numCircles = updateCount;

    const float timestep = config->simulation.timestep;
    for (int i = 0; i < updateCount; i++) {
        updateCircleFromFingertip(
            circles[i], fingertips[i], gridX, gridY, baseCircleRadius,
            config->simulation.circle, timestep);
    }

    // clear any circles that are no longer present
    for (int i = updateCount; i < HandTracking::MAX_CIRCLES; i++) {
        circles[i].present = false;
        circles[i].wasPresent = false;
        circles[i].x = 0;
        circles[i].y = 0;
        circles[i].prevX = 0;
        circles[i].prevY = 0;
        circles[i].smoothedX = 0.0f;
        circles[i].smoothedY = 0.0f;
        circles[i].velX = 0.0f;
        circles[i].velY = 0.0f;
        circles[i].z = 0.0f;
        circles[i].scaledRadius = 0;
    }
}

void GPUSimulator::updateLineSegments(const FingertipData* landmarks, int count) {
    // requires updateCircles to have run first
    numSegments = 0;
    numPresentSegments = 0;
    int numHands = std::min(2, count / HandTracking::LANDMARKS_PER_HAND);

    for (int hand = 0; hand < numHands; hand++) {
        int offset = hand * HandTracking::LANDMARKS_PER_HAND;

        // check if hand is present
        bool handPresent = false;
        for (int i = 0; i < HandTracking::LANDMARKS_PER_HAND; i++) {
            if (landmarks[offset + i].present > 0.5f) {
                handPresent = true;
                break;
            }
        }
        if (!handPresent) continue;

        // create segments for each connection
        for (int conn = 0; conn < HandTracking::MAX_CONNECTIONS && numSegments < HandTracking::MAX_SEGMENTS; conn++) {
            int idx1 = HandTracking::HAND_CONNECTIONS[conn][0];
            int idx2 = HandTracking::HAND_CONNECTIONS[conn][1];

            const FingertipData& p1 = landmarks[offset + idx1];
            const FingertipData& p2 = landmarks[offset + idx2];

            LineSegment& seg = segments[numSegments];
            seg.wasPresent = seg.present;
            seg.prevStartX = seg.startX;
            seg.prevStartY = seg.startY;
            seg.prevEndX = seg.endX;
            seg.prevEndY = seg.endY;
            seg.prevStartRadius = seg.startRadius;
            seg.prevEndRadius = seg.endRadius;

            seg.present = (p1.present > 0.5f) && (p2.present > 0.5f);

            if (seg.present) {
                int startIdx = offset + idx1;
                int endIdx = offset + idx2;
                seg.startX = circles[startIdx].x;
                seg.startY = circles[startIdx].y;
                seg.endX = circles[endIdx].x;
                seg.endY = circles[endIdx].y;
                seg.startRadius = static_cast<float>(scaleRadiusByZ(p1.z, baseCircleRadius));
                seg.endRadius = static_cast<float>(scaleRadiusByZ(p2.z, baseCircleRadius));
                seg.startVelX = circles[startIdx].velX;
                seg.startVelY = circles[startIdx].velY;
                seg.endVelX = circles[endIdx].velX;
                seg.endVelY = circles[endIdx].velY;
                numPresentSegments++;
            } else {
                if (seg.wasPresent) {
                    // clear previous segment
                    seg.startX = 0;
                    seg.startY = 0;
                    seg.endX = 0;
                    seg.endY = 0;
                }
                seg.startVelX = 0.0f;
                seg.startVelY = 0.0f;
                seg.endVelX = 0.0f;
                seg.endVelY = 0.0f;
            }

            numSegments++;
        }
    }

    // clear remaining segments
    for (int i = numSegments; i < HandTracking::MAX_SEGMENTS; i++) {
        segments[i].present = false;
        segments[i].wasPresent = false;
        segments[i].startX = 0;
        segments[i].startY = 0;
        segments[i].endX = 0;
        segments[i].endY = 0;
        segments[i].startRadius = 0.0f;
        segments[i].endRadius = 0.0f;
        segments[i].startVelX = 0.0f;
        segments[i].startVelY = 0.0f;
        segments[i].endVelX = 0.0f;
        segments[i].endVelY = 0.0f;
    }
}

// DISPATCH COMPUTE SHADERS
// Segments and circles share one pass: the shader writes every interior texel, so the
// pre-copies only exist to keep the border ring valid through the ping-pong.
void GPUSimulator::dispatchInput(WGPUCommandEncoder encoder) {
    copyTextureDeviceToDevice(encoder, velocityTexture, newVelocityTexture);
    copyTextureDeviceToDevice(encoder, densityTexture, newDensityTexture);
    copyTextureDeviceToDevice(encoder, solidTexture, newSolidTexture);
    dispatchComputePass(encoder, inputPipeline, inputBindGroup);
    copyTextureDeviceToDevice(encoder, newVelocityTexture, velocityTexture);
    copyTextureDeviceToDevice(encoder, newSolidTexture, solidTexture);
    copyTextureDeviceToDevice(encoder, newDensityTexture, densityTexture);
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

    int iterations = config->simulation.projection.iterations;
    if (isGpuJacobi(config->pipeline)) {
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

        if (iterations % 2 == 1) {
            copyTextureDeviceToDevice(encoder, newPressureTexture, pressureTexture);
        }
    } else if (isGpuGaussSeidel(config->pipeline)) {
        for (int iter = 0; iter < iterations; iter++) {
            dispatchComputePass(encoder, gsPipeline, jacobiBindGroup);
            dispatchComputePass(encoder, gsBlackPipeline, jacobiPingPongBindGroup);
        }
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
    cmdDesc.label = WGPU_CSTR(desc.label);
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
    uint32_t initialMinMax[8] = {
        floatToOrderedUint(positiveInf),
        floatToOrderedUint(negativeInf),
        floatToOrderedUint(positiveInf),
        floatToOrderedUint(negativeInf),
        floatToOrderedUint(positiveInf),
        floatToOrderedUint(negativeInf),
        0u, // densitySumScaled
        0u  // fluidCellCount
    };
    wgpuQueueWriteBuffer(queue, slot.minMaxBuffer, 0, initialMinMax, sizeof(initialMinMax));

    WGPUBindGroupEntry minMaxEntries[6] = {};
    minMaxEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    minMaxEntries[1] = createTextureViewBindGroupEntry(1, pressureTextureView);
    minMaxEntries[2] = createTextureViewBindGroupEntry(2, velocityTextureView);
    minMaxEntries[3] = createTextureViewBindGroupEntry(3, solidTextureView);
    minMaxEntries[4] = createStorageBufferBindGroupEntry(4, slot.minMaxBuffer, 8 * sizeof(uint32_t));
    minMaxEntries[5] = createTextureViewBindGroupEntry(5, densityTextureView);

    if (slot.bindGroupMinMax) {
        wgpuBindGroupRelease(slot.bindGroupMinMax);
    }
    slot.bindGroupMinMax = createBindGroup(6, minMaxEntries, pressureMinMaxBindGroupLayout);

    // dispatch compute pass
    HistogramDispatchDesc desc = {};
    desc.pipeline = pressureMinMaxPipeline;
    desc.bindGroup = slot.bindGroupMinMax;
    desc.srcBuffer = slot.minMaxBuffer;
    desc.stagingBuffer = slot.minMaxStagingBuffer;
    desc.stagingBufferSize = 8 * sizeof(uint32_t);
    desc.label = "Histogram MinMax";
    desc.slotIndex = slotIndex;
    desc.simulator = this;
    dispatchHistogramCompute(desc);

    // map staging buffer asynchronously with callback
#ifdef WEBGPU_BACKEND_EMDAWNWEBGPU
    WGPUBufferMapCallbackInfo mapCallbackInfo = {};
    mapCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    mapCallbackInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2) {
        (void)message;
        GPUSimulator* sim = static_cast<GPUSimulator*>(userdata1);
        int slotIdx = static_cast<int>(reinterpret_cast<intptr_t>(userdata2));
        sim->onMinMaxMapped(status, slotIdx);
    };
    mapCallbackInfo.userdata1 = this;
    mapCallbackInfo.userdata2 = reinterpret_cast<void*>(static_cast<intptr_t>(slotIndex));
    wgpuBufferMapAsync(slot.minMaxStagingBuffer, WGPUMapMode_Read, 0, 8 * sizeof(uint32_t), mapCallbackInfo);
#else
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
    wgpuBufferMapAsync2(slot.minMaxStagingBuffer, WGPUMapMode_Read, 0, 8 * sizeof(uint32_t), mapCallbackInfo);
#endif
}

void GPUSimulator::onMinMaxMapped(WGPUMapAsyncStatus status, int slotIndex) {
    auto& slot = histogramSlots[slotIndex];

    if (status == WGPUMapAsyncStatus_Success) {
        const uint32_t* data = static_cast<const uint32_t*>(
            wgpuBufferGetConstMappedRange(slot.minMaxStagingBuffer, 0, 8 * sizeof(uint32_t))
        );

        if (data) {
            slot.pendingPressureMinMax[0] = orderedUintToFloat(data[0]);  // pressMin
            slot.pendingPressureMinMax[1] = orderedUintToFloat(data[1]);  // pressMax
            slot.pendingVelocityMinMax[0] = orderedUintToFloat(data[2]);  // velMin
            slot.pendingVelocityMinMax[1] = orderedUintToFloat(data[3]);  // velMax
            slot.pendingDensityMinMax[0] = orderedUintToFloat(data[4]);   // densityMin
            slot.pendingDensityMinMax[1] = orderedUintToFloat(data[5]);   // densityMax
            slot.pendingDensitySumScaled = data[6];
            slot.pendingFluidCellCount = data[7];
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
    MinMaxUniform minMaxUniform = {
        slot.pendingPressureMinMax[0],
        slot.pendingPressureMinMax[1],
        slot.pendingVelocityMinMax[0],
        slot.pendingVelocityMinMax[1]
    };
    wgpuQueueWriteBuffer(queue, slot.minMaxUniformBuffer, 0, &minMaxUniform, sizeof(MinMaxUniform));

    WGPUBindGroupEntry histBinsEntries[6] = {};
    histBinsEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    histBinsEntries[1] = createUniformBufferBindGroupEntry(1, slot.minMaxUniformBuffer, sizeof(MinMaxUniform));
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
#ifdef WEBGPU_BACKEND_EMDAWNWEBGPU
    WGPUBufferMapCallbackInfo binMapCallbackInfo = {};
    binMapCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    binMapCallbackInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2) {
        (void)message;
        GPUSimulator* sim = static_cast<GPUSimulator*>(userdata1);
        int slotIdx = static_cast<int>(reinterpret_cast<intptr_t>(userdata2));
        sim->onHistogramBinsMapped(status, slotIdx);
    };
    binMapCallbackInfo.userdata1 = this;
    binMapCallbackInfo.userdata2 = reinterpret_cast<void*>(static_cast<intptr_t>(slotIndex));
    wgpuBufferMapAsync(slot.histogramStagingBuffer, WGPUMapMode_Read, 0, 128 * sizeof(int32_t), binMapCallbackInfo);
#else
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
#endif
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

bool GPUSimulator::getHistogramData(
    int& readySlot,
    const float*& pressureMinMax,
    const float*& velocityMinMax,
    const float*& densityMinMax,
    const int*& histogramBins,
    uint32_t& densitySumScaled,
    uint32_t& fluidCellCount
) const {
    // find the most recent ready slot
    for (int i = 0; i < HISTOGRAM_RING_SIZE; i++) {
        int checkIdx = (histogramReadIndex + i) % HISTOGRAM_RING_SIZE;
        if (histogramSlots[checkIdx].state == HistogramSlot_Ready) {
            readySlot = checkIdx;
            auto& slot = histogramSlots[checkIdx];
            pressureMinMax = slot.pendingPressureMinMax;
            velocityMinMax = slot.pendingVelocityMinMax;
            densityMinMax = slot.pendingDensityMinMax;
            histogramBins = slot.pendingHistogramBins;
            densitySumScaled = slot.pendingDensitySumScaled;
            fluidCellCount = slot.pendingFluidCellCount;
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
    uniformDesc.size = sizeof(SimParams);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformDesc.mappedAtCreation = false;

    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);
    RETURN_FALSE_IF_FAIL(uniformBuffer);

    circleBuffer = createBuffer(
        HandTracking::MAX_CIRCLES * sizeof(CircleGPU),
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    RETURN_FALSE_IF_FAIL(circleBuffer);

    segmentBuffer = createBuffer(
        HandTracking::MAX_SEGMENTS * sizeof(SegmentGPU),
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    RETURN_FALSE_IF_FAIL(segmentBuffer);

    return true;
}

bool GPUSimulator::initSimData(const Config& cfg, const ImageData* imageData, float aspectRatio) {
    this->config = &g_config;
    if (!cpuSimulator.init(cfg, imageData, aspectRatio)) {
        return false;
    }

    syncGridFromCpu();
    return true;
}

void GPUSimulator::syncGridFromCpu() {
    resolution = g_config.simulation.resolution;
    cellSize = cpuSimulator.cellSize;
    gridX = cpuSimulator.gridX;
    gridY = cpuSimulator.gridY;
    domainWidth = cpuSimulator.domainWidth;
    domainHeight = cpuSimulator.domainHeight;
    workgroupX = (gridX + 15) / 16;
    workgroupY = (gridY + 15) / 16;
    windTunnelStartCell = cpuSimulator.windTunnelStartCell;
    windTunnelEndCell = cpuSimulator.windTunnelEndCell;
    pipeHeight = cpuSimulator.pipeHeight;

    mouseCircleRadius = cpuSimulator.mouseCircleRadius;
    mouseCircleX = cpuSimulator.mouseCircleX;
    mouseCircleY = cpuSimulator.mouseCircleY;
    mousePrevCircleX = cpuSimulator.mousePrevCircleX;
    mousePrevCircleY = cpuSimulator.mousePrevCircleY;
    mouseCircleVelX = cpuSimulator.mouseCircleVelX;
    mouseCircleVelY = cpuSimulator.mouseCircleVelY;
    isMouseDragging = cpuSimulator.isMouseDragging;
    mouseDragNeedsResync = cpuSimulator.mouseDragNeedsResync;

    baseCircleRadius = cpuSimulator.baseCircleRadius;
    numCircles = cpuSimulator.numCircles;
    numSegments = cpuSimulator.numSegments;
    numPresentSegments = cpuSimulator.numPresentSegments;
    for (int i = 0; i < HandTracking::MAX_CIRCLES; i++) {
        circles[i] = cpuSimulator.circles[i];
    }
    for (int i = 0; i < HandTracking::MAX_SEGMENTS; i++) {
        segments[i] = cpuSimulator.segments[i];
    }

    inkInitialized = cpuSimulator.inkInitialized;
}

void GPUSimulator::releaseSimGridResources() {
#define RELEASE_COMPUTE_BIND_GROUP(name) releaseResource(name##BindGroup, wgpuBindGroupRelease);

    RELEASE_COMPUTE_BIND_GROUP(divergence)
    RELEASE_COMPUTE_BIND_GROUP(jacobi)
    RELEASE_COMPUTE_BIND_GROUP(jacobiPingPong)
    RELEASE_COMPUTE_BIND_GROUP(velocityUpdate)
    RELEASE_COMPUTE_BIND_GROUP(extrapolate)
    RELEASE_COMPUTE_BIND_GROUP(advectVelocity)
    RELEASE_COMPUTE_BIND_GROUP(advectDensity)
    RELEASE_COMPUTE_BIND_GROUP(advectInk)
    RELEASE_COMPUTE_BIND_GROUP(boundary)
    RELEASE_COMPUTE_BIND_GROUP(vorticityCompute)
    RELEASE_COMPUTE_BIND_GROUP(vorticityApply)
    RELEASE_COMPUTE_BIND_GROUP(input)
    RELEASE_COMPUTE_BIND_GROUP(pressureMinMax)
    RELEASE_COMPUTE_BIND_GROUP(histogramBins)

    for (int i = 0; i < HISTOGRAM_RING_SIZE; i++) {
        auto& slot = histogramSlots[i];
        releaseResource(slot.bindGroupMinMax, wgpuBindGroupRelease);
        releaseResource(slot.bindGroupHistogramBins, wgpuBindGroupRelease);
        slot.bindGroupMinMax = nullptr;
        slot.bindGroupHistogramBins = nullptr;
        slot.state = HistogramSlot_Free;
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
    RELEASE_TEXTURE(newSolid)

#undef RELEASE_COMPUTE_BIND_GROUP
}

bool GPUSimulator::rebuildGridResources() {
    if (!device) {
        return true;
    }

    releaseSimGridResources();
    RETURN_FALSE_IF_FAIL(initSimTextures());
    if (divergenceBindGroupLayout != nullptr) {
        RETURN_FALSE_IF_FAIL(initBindGroups());
    }
    return true;
}

void GPUSimulator::zeroScalarGpuTexture(WGPUTexture texture) {
    if (!device || !texture || gridX <= 0 || gridY <= 0) {
        return;
    }

    const size_t cellCount = static_cast<size_t>(gridX) * static_cast<size_t>(gridY);
    std::vector<float> zeros(cellCount, 0.0f);

    WGPUExtent3D size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    WGPUImageCopyTexture copy = {};
    copy.mipLevel = 0;
    copy.origin = {0, 0, 0};
    copy.aspect = WGPUTextureAspect_All;
    copy.texture = texture;

    WGPUTextureDataLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = gridX * sizeof(float);
    layout.rowsPerImage = gridY;

    wgpuQueueWriteTexture(queue, &copy, zeros.data(), zeros.size() * sizeof(float), &layout, &size);
}

void GPUSimulator::uploadScalarGpuFieldsFromCpu() {
    if (!device || !velocityTexture) {
        return;
    }

    const auto& velX = cpuSimulator.getVelocityX();
    const auto& velY = cpuSimulator.getVelocityY();
    const auto& solid = cpuSimulator.getSolid();
    const auto& density = cpuSimulator.getDensity();

    WGPUExtent3D size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};

    std::vector<float> velocityData(gridX * gridY * 2);
    for (int j = 0; j < gridY; j++) {
        for (int i = 0; i < gridX; i++) {
            int idx = j * gridX + i;
            velocityData[idx * 2] = velX[idx];
            velocityData[idx * 2 + 1] = velY[idx];
        }
    }

    WGPUImageCopyTexture copy = {};
    copy.mipLevel = 0;
    copy.origin = {0, 0, 0};
    copy.aspect = WGPUTextureAspect_All;

    WGPUTextureDataLayout layout = {};
    layout.offset = 0;
    layout.rowsPerImage = gridY;

    layout.bytesPerRow = gridX * 8;
    copy.texture = velocityTexture;
    wgpuQueueWriteTexture(queue, &copy, velocityData.data(), velocityData.size() * sizeof(float), &layout, &size);
    copy.texture = newVelocityTexture;
    wgpuQueueWriteTexture(queue, &copy, velocityData.data(), velocityData.size() * sizeof(float), &layout, &size);

    layout.bytesPerRow = gridX * 4;
    copy.texture = densityTexture;
    wgpuQueueWriteTexture(queue, &copy, density.data(), density.size() * sizeof(float), &layout, &size);
    copy.texture = newDensityTexture;
    wgpuQueueWriteTexture(queue, &copy, density.data(), density.size() * sizeof(float), &layout, &size);

    copy.texture = solidTexture;
    wgpuQueueWriteTexture(queue, &copy, solid.data(), solid.size() * sizeof(float), &layout, &size);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
    copyTextureDeviceToDevice(encoder, solidTexture, newSolidTexture);
    copyTextureDeviceToDevice(encoder, densityTexture, newDensityTexture);
    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    zeroScalarGpuTexture(pressureTexture);
    zeroScalarGpuTexture(newPressureTexture);
    zeroScalarGpuTexture(divergenceTexture);
    zeroScalarGpuTexture(curlTexture);
}

bool GPUSimulator::initSimTextures() {
    // RG32Float to store channels (x,y) together
    CREATE_STORAGE_TEXTURE(velocity, RG32Float);
    CREATE_STORAGE_TEXTURE(newVelocity, RG32Float);

    CREATE_STORAGE_TEXTURE(pressure, R32Float);
    CREATE_STORAGE_TEXTURE(newPressure, R32Float);

    CREATE_STORAGE_TEXTURE(density, R32Float);
    CREATE_STORAGE_TEXTURE(newDensity, R32Float);

    // stored as a float because WebGPU is shit
    CREATE_STORAGE_TEXTURE(solid, R32Float);
    CREATE_STORAGE_TEXTURE(newSolid, R32Float);

    // RGBA32Float to store all 3 channels (r,g,b) together
    CREATE_STORAGE_TEXTURE(ink, RGBA32Float);
    CREATE_STORAGE_TEXTURE(newInk, RGBA32Float);

    // these require their own textures because we compute all values in parallel
    // as opposed to more expensive texture sampling in the shaders that use these values
    CREATE_STORAGE_TEXTURE(divergence, R32Float);
    CREATE_STORAGE_TEXTURE(curl, R32Float);

    return true;
}

bool GPUSimulator::initHistogramResources() {
    for (int i = 0; i < HISTOGRAM_RING_SIZE; i++) {
        auto& slot = histogramSlots[i];

        WGPUBufferDescriptor minMaxBufferDesc = {};
        minMaxBufferDesc.size = 8 * sizeof(uint32_t);
        minMaxBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        minMaxBufferDesc.mappedAtCreation = true;
        slot.minMaxBuffer = wgpuDeviceCreateBuffer(device, &minMaxBufferDesc);
        if (!slot.minMaxBuffer) return false;

        // initialize with +/- inf
        uint32_t* mappedData = static_cast<uint32_t*>(wgpuBufferGetMappedRange(slot.minMaxBuffer, 0, 8 * sizeof(uint32_t)));
        const float positiveInf = std::numeric_limits<float>::infinity();
        const float negativeInf = -std::numeric_limits<float>::infinity();
        mappedData[0] = floatToOrderedUint(positiveInf);
        mappedData[1] = floatToOrderedUint(negativeInf);
        mappedData[2] = floatToOrderedUint(positiveInf);
        mappedData[3] = floatToOrderedUint(negativeInf);
        mappedData[4] = floatToOrderedUint(positiveInf);
        mappedData[5] = floatToOrderedUint(negativeInf);
        mappedData[6] = 0u; // densitySumScaled
        mappedData[7] = 0u; // fluidCellCount
        wgpuBufferUnmap(slot.minMaxBuffer);

        slot.minMaxStagingBuffer = createBuffer(8 * sizeof(uint32_t), WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
        RETURN_FALSE_IF_FAIL(slot.minMaxStagingBuffer);

        slot.histogramBinBuffer = createBuffer(128 * sizeof(int32_t), WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
        RETURN_FALSE_IF_FAIL(slot.histogramBinBuffer);

        slot.histogramStagingBuffer = createBuffer(128 * sizeof(int32_t), WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
        RETURN_FALSE_IF_FAIL(slot.histogramStagingBuffer);

        slot.minMaxUniformBuffer = createBuffer(sizeof(MinMaxUniform), WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
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

bool GPUSimulator::initTextures() {
    RETURN_FALSE_IF_FAIL(initSimTextures());
    if (histogramSlots[0].minMaxBuffer == nullptr) {
        RETURN_FALSE_IF_FAIL(initHistogramResources());
    }
    return true;
}

bool GPUSimulator::initPipelineLayouts() {
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

    // jacobi / gauss-seidel pressure solver [5]:
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

    // merged hand/mouse input update [9]:
    // uniform
    // solid texture (sampled)
    // velocity (storage, read)
    // new velocity (storage, write)
    // density texture (sampled)
    // new solid (storage, write)
    // new density (storage, write)
    // circles (storage, read)
    // segments (storage, read)
    WGPUBindGroupLayoutEntry inputLayoutEntries[9] = {};
    createUniformBufferPipelineLayoutEntry(inputLayoutEntries);
    inputLayoutEntries[1].binding = 1;
    inputLayoutEntries[1].visibility = WGPUShaderStage_Compute;
    inputLayoutEntries[1].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    inputLayoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    inputLayoutEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    inputLayoutEntries[3] = createStorageTextureLayoutEntry(3, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RG32Float);
    inputLayoutEntries[4].binding = 4;
    inputLayoutEntries[4].visibility = WGPUShaderStage_Compute;
    inputLayoutEntries[4].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    inputLayoutEntries[4].texture.viewDimension = WGPUTextureViewDimension_2D;
    inputLayoutEntries[5] = createStorageTextureLayoutEntry(5, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_R32Float);
    inputLayoutEntries[6] = createStorageTextureLayoutEntry(6, WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_R32Float);
    inputLayoutEntries[7] = createStorageBufferLayoutEntry(7, sizeof(CircleGPU), true);
    inputLayoutEntries[8] = createStorageBufferLayoutEntry(8, sizeof(SegmentGPU), true);

    inputBindGroupLayout = createBindGroupLayout(9, inputLayoutEntries);
    RETURN_FALSE_IF_FAIL(inputBindGroupLayout);
    inputPipelineLayout = createPipelineLayout(&inputBindGroupLayout);
    RETURN_FALSE_IF_FAIL(inputPipelineLayout);

    // pressure minmax [6]:
    // uniform
    // pressure (read)
    // velocity (read)
    // solid (read)
    // minmax storage buffer
    // density (read)
    WGPUBindGroupLayoutEntry pressureMinMaxEntries[6] = {};
    createUniformBufferPipelineLayoutEntry(pressureMinMaxEntries);
    pressureMinMaxEntries[1] = createStorageTextureLayoutEntry(1, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);
    pressureMinMaxEntries[2] = createStorageTextureLayoutEntry(2, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_RG32Float);
    pressureMinMaxEntries[3] = createStorageTextureLayoutEntry(3, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);
    pressureMinMaxEntries[4] = createStorageBufferLayoutEntry(4, 8 * sizeof(uint32_t));
    pressureMinMaxEntries[5] = createStorageTextureLayoutEntry(5, WGPUStorageTextureAccess_ReadOnly, WGPUTextureFormat_R32Float);

    pressureMinMaxBindGroupLayout = createBindGroupLayout(6, pressureMinMaxEntries);
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
    histogramBinsEntries[1] = createUniformBufferLayoutEntry(1, sizeof(MinMaxUniform));
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
    // divergence [4]
    WGPUBindGroupEntry divergenceEntries[4] = {};
    divergenceEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    divergenceEntries[1] = createTextureViewBindGroupEntry(1, velocityTextureView);
    divergenceEntries[2] = createTextureViewBindGroupEntry(2, divergenceTextureView);
    divergenceEntries[3] = createTextureViewBindGroupEntry(3, solidTextureView);

    divergenceBindGroup = createBindGroup(4, divergenceEntries, divergenceBindGroupLayout);
    RETURN_FALSE_IF_FAIL(divergenceBindGroup);

    // jacobi / gauss-seidel [5]
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

    // merged input update [9]
    WGPUBindGroupEntry inputEntries[9] = {};
    inputEntries[0] = createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(SimParams));
    inputEntries[1] = createTextureViewBindGroupEntry(1, solidTextureView);
    inputEntries[2] = createTextureViewBindGroupEntry(2, velocityTextureView);
    inputEntries[3] = createTextureViewBindGroupEntry(3, newVelocityTextureView);
    inputEntries[4] = createTextureViewBindGroupEntry(4, densityTextureView);
    inputEntries[5] = createTextureViewBindGroupEntry(5, newSolidTextureView);
    inputEntries[6] = createTextureViewBindGroupEntry(6, newDensityTextureView);
    inputEntries[7] = createStorageBufferBindGroupEntry(7, circleBuffer, HandTracking::MAX_CIRCLES * sizeof(CircleGPU));
    inputEntries[8] = createStorageBufferBindGroupEntry(8, segmentBuffer, HandTracking::MAX_SEGMENTS * sizeof(SegmentGPU));

    inputBindGroup = createBindGroup(9, inputEntries, inputBindGroupLayout);
    RETURN_FALSE_IF_FAIL(inputBindGroup);

    // histogram bind groups are created dynamically per-slot in dispatch methods

    return true;
}

bool GPUSimulator::initPipelines() {
    // create compute pipelines
    divergencePipeline = createComputePipeline("compute_divergence.wgsl", "divergence", divergencePipelineLayout);
    RETURN_FALSE_IF_FAIL(divergencePipeline);
    jacobiPipeline = createComputePipeline("compute_jacobi.wgsl", "jacobiIteration", jacobiPipelineLayout);
    RETURN_FALSE_IF_FAIL(jacobiPipeline);
    gsPipeline = createComputePipeline("compute_gs.wgsl", "gsRedIteration", jacobiPipelineLayout);
    RETURN_FALSE_IF_FAIL(gsPipeline);
    gsBlackPipeline = createComputePipeline("compute_gs.wgsl", "gsBlackIteration", jacobiPipelineLayout);
    RETURN_FALSE_IF_FAIL(gsBlackPipeline);
    velocityUpdatePipeline = createComputePipeline("compute_apply_projection.wgsl", "applyProjection", velocityUpdatePipelineLayout);
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

    inputPipeline = createComputePipeline("compute_input.wgsl", "updateInput", inputPipelineLayout);
    RETURN_FALSE_IF_FAIL(inputPipeline);

    pressureMinMaxPipeline = createComputePipeline("compute_pressure_minmax.wgsl", "computePressureMinMax", pressureMinMaxPipelineLayout);
    RETURN_FALSE_IF_FAIL(pressureMinMaxPipeline);

    histogramBinsPipeline = createComputePipeline("compute_histogram_bins.wgsl", "computeHistogramBins", histogramBinsPipelineLayout);
    RETURN_FALSE_IF_FAIL(histogramBinsPipeline);

    return true;
}

void GPUSimulator::updateSimParams(const Config& config) {
    const bool gridChanged = cpuSimulator.gridConfigChanged(config);
    cpuSimulator.updateSimParams(config);
    this->config = &g_config;

    momentumTransferStrength = config.simulation.circle.momentumTransferStrength;
    momentumTransferRadius = config.simulation.circle.momentumTransferRadius;

    if (gridChanged) {
        syncGridFromCpu();
        if (device) {
            rebuildGridResources();
        }
        return;
    }

    windTunnelSide = config.simulation.windTunnel.side;
    windTunnelSpeed = config.simulation.windTunnel.velocity;
    windTunnelStartCell = cpuSimulator.windTunnelStartCell;
    windTunnelEndCell = cpuSimulator.windTunnelEndCell;
    pipeHeight = cpuSimulator.pipeHeight;

    mouseCircleRadius = cpuSimulator.mouseCircleRadius;
    baseCircleRadius = cpuSimulator.baseCircleRadius;
    for (int i = 0; i < HandTracking::MAX_CIRCLES; i++) {
        circles[i].scaledRadius = baseCircleRadius;
    }

    updateUniformBufferSim();
}

void GPUSimulator::reinitInk(const ImageData* imageData) {
    // Reset fluid state (this will call resetFluidState on cpuSimulator and re-upload textures)
    resetFluidState();

    if (imageData) {
        // Initialize new ink data on CPU simulator (initializeFromImageData is now public)
        cpuSimulator.initializeFromImageData(g_config, imageData);

        const auto& inkData = cpuSimulator.getInk();

        // Re-upload to both ping-pong textures
        WGPUExtent3D size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
        WGPUImageCopyTexture copy = {};
        copy.mipLevel = 0;
        copy.origin = {0, 0, 0};
        copy.aspect = WGPUTextureAspect_All;

        WGPUTextureDataLayout layout = {};
        layout.offset = 0;
        layout.bytesPerRow = gridX * 16; // RGBA32Float = 4 floats * 4 bytes
        layout.rowsPerImage = gridY;

        copy.texture = inkTexture;
        wgpuQueueWriteTexture(queue, &copy, inkData.data(), inkData.size() * sizeof(float), &layout, &size);

        copy.texture = newInkTexture;
        wgpuQueueWriteTexture(queue, &copy, inkData.data(), inkData.size() * sizeof(float), &layout, &size);

        inkInitialized = true;
        cpuSimulator.inkInitialized = true;
    } else {
        inkInitialized = false;
        cpuSimulator.inkInitialized = false;
    }
    if (uniformBuffer) {
        updateUniformBufferSim();
    }
}

void GPUSimulator::resetFluidState(bool clearInk) {
    for (int i = 0; i < HandTracking::MAX_CIRCLES; i++) {
        circles[i].velX = 0.0f;
        circles[i].velY = 0.0f;
    }

    cpuSimulator.resetFluidState(clearInk);
    inkInitialized = cpuSimulator.inkInitialized;

    if (!device || !velocityTexture) {
        return;
    }

    uploadScalarGpuFieldsFromCpu();

    if (clearInk) {
        WGPUExtent3D size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
        std::vector<float> zeroInk(gridX * gridY * 4, 0.0f);

        WGPUImageCopyTexture copy = {};
        copy.mipLevel = 0;
        copy.origin = {0, 0, 0};
        copy.aspect = WGPUTextureAspect_All;

        WGPUTextureDataLayout layout = {};
        layout.offset = 0;
        layout.bytesPerRow = gridX * 16;
        layout.rowsPerImage = gridY;

        copy.texture = inkTexture;
        wgpuQueueWriteTexture(queue, &copy, zeroInk.data(), zeroInk.size() * sizeof(float), &layout, &size);
        copy.texture = newInkTexture;
        wgpuQueueWriteTexture(queue, &copy, zeroInk.data(), zeroInk.size() * sizeof(float), &layout, &size);

        inkInitialized = false;
        cpuSimulator.inkInitialized = false;
    }
    if (uniformBuffer) {
        updateUniformBufferSim();
    }
}
