#include "gpu_sim.h"
#include <iostream>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

GPUFluidSimulator::GPUFluidSimulator(const Config& config)
    : 
    cpuSimulator(config),
    device(nullptr),
    queue(nullptr),
    webgpuInitialized(false),
    velocityTexture(nullptr),
    pressureTexture(nullptr),
    densityTexture(nullptr),
    solidTexture(nullptr),
    inkTexture(nullptr),
    divergenceTexture(nullptr),
    newVelocityTexture(nullptr),
    newDensityTexture(nullptr),
    newInkTexture(nullptr),
    velocityTextureView(nullptr),
    pressureTextureView(nullptr),
    densityTextureView(nullptr),
    solidTextureView(nullptr),
    inkTextureView(nullptr),
    divergenceTextureView(nullptr),
    newVelocityTextureView(nullptr),
    newDensityTextureView(nullptr),
    newInkTextureView(nullptr),
    sampler(nullptr),
    uniformBuffer(nullptr),
    advectPipelineLayout(nullptr),
    divergenceBindGroupLayout(nullptr),
    jacobiBindGroupLayout(nullptr),
    velocityUpdateBindGroupLayout(nullptr),
    divergencePipelineLayout(nullptr),
    jacobiPipelineLayout(nullptr),
    velocityUpdatePipelineLayout(nullptr),
    velocityBindGroupLayout(nullptr),
    pressureBindGroupLayout(nullptr),
    densityBindGroupLayout(nullptr),
    inkBindGroupLayout(nullptr),
    uniformBindGroupLayout(nullptr),
    velocityBindGroup(nullptr),
    pressureBindGroup(nullptr),
    densityBindGroup(nullptr),
    inkBindGroup(nullptr),
    uniformBindGroup(nullptr),
    divergenceBindGroup(nullptr),
    jacobiBindGroup(nullptr),
    jacobiPingPongBindGroup(nullptr),
    velocityUpdateBindGroup(nullptr),
    extrapolatePipeline(nullptr),
    extrapolatePipelineLayout(nullptr),
    extrapolateBindGroupLayout(nullptr),
    extrapolateBindGroup(nullptr),
    advectVelocityPipeline(nullptr),
    advectDensityPipeline(nullptr),
    advectInkPipeline(nullptr),
    advectVelocityPipelineLayout(nullptr),
    advectDensityPipelineLayout(nullptr),
    advectInkPipelineLayout(nullptr),
    advectVelocityBindGroupLayout(nullptr),
    advectDensityBindGroupLayout(nullptr),
    advectInkBindGroupLayout(nullptr),
    advectVelocityBindGroup(nullptr),
    advectDensityBindGroup(nullptr),
    advectInkBindGroup(nullptr),
    boundaryPipeline(nullptr),
    boundaryPipelineLayout(nullptr),
    boundaryBindGroupLayout(nullptr),
    boundaryBindGroup(nullptr),
    vorticityComputePipelineLayout(nullptr),
    vorticityApplyPipelineLayout(nullptr),
    vorticityComputeBindGroupLayout(nullptr),
    vorticityApplyBindGroupLayout(nullptr),
    vorticityComputeBindGroup(nullptr),
    vorticityApplyBindGroup(nullptr),
    vorticityComputePipeline(nullptr),
    vorticityApplyPipeline(nullptr),
    curlTexture(nullptr),
    curlTextureView(nullptr),
    gridX(0),
    gridY(0),

    // Circle state initialization (matching CPU implementation)
    circleX(0),
    circleY(0),
    prevCircleX(0),
    prevCircleY(0),
    circleVelX(0.0f),
    circleVelY(0.0f),
    circleRadius(config.simulation.circle.radius),
    isDragging(false),
    circleWasMoved(false),

    // Circle momentum transfer parameters
    momentumTransferCoeff(config.simulation.circle.momentumTransferCoeff),
    momentumTransferRadius(config.simulation.circle.momentumTransferRadius),

    // Circle pipeline
    circlePipelineLayout(nullptr),
    circleBindGroupLayout(nullptr),
    circleBindGroup(nullptr),
    circlePipeline(nullptr) {
}

GPUFluidSimulator::~GPUFluidSimulator() {
    releaseGPUResources();
}

bool GPUFluidSimulator::initWebGPU(WGPUDevice device, WGPUQueue queue) {
    if (!device || !queue) {
        std::cerr << "Error: Invalid WebGPU device or queue provided to GPUFluidSimulator" << std::endl;
        return false;
    }

    this->device = device;
    this->queue = queue;

    webgpuInitialized = true;
    return true;
}

void GPUFluidSimulator::init(const Config& cfg, const ImageData* imageData) {
    this->config = &cfg;
    cpuSimulator.init(cfg, imageData);

    gridX = cpuSimulator.getGridX();
    gridY = cpuSimulator.getGridY();
    cellSize = cpuSimulator.getCellSize();

    // Initialize circle position (matching CPU implementation)
    circleX = gridX / 2;
    circleY = gridY / 2;
    prevCircleX = circleX;
    prevCircleY = circleY;

    if (webgpuInitialized && device && queue) {
        if (!initGPUResources()) {
            std::cerr << "Error: Failed to initialize GPU resources after CPU init" << std::endl;
        }
        copyInitialDataToGPU(); // Copy data AFTER GPU resources are initialized
    }
}

void GPUFluidSimulator::update() {
    // Update uniform buffer each frame
    updateUniformBuffer();

    // Reset circle movement flag at start of each frame
    circleWasMoved = false;

    // Create a single command encoder for the entire update
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

    // Apply gravity if enabled
    if (config->simulation.gravity != 0.0f) {
        dispatchIntegrate(encoder);
    }

    // Update circle position and apply effects (must come before advection)
    dispatchCircle(encoder);

    dispatchProjection(encoder);
    dispatchExtrapolate(encoder);

    dispatchAdvect(encoder);
    dispatchBoundaryConditions(encoder); // TEMP NOT IDEAL

    // Apply vorticity confinement if enabled
    if (config->simulation.vorticity.enabled) {
        dispatchVorticity(encoder);
    }

    // Submit all commands at once
    WGPUCommandBufferDescriptor cmdDesc = {};
    cmdDesc.label = "Simulation Update";
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &commands);

    // clean up
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

bool GPUFluidSimulator::isInsideCircle(int i, int j) {
    float dx = (i + 0.5f) - circleX;
    float dy = (j + 0.5f) - circleY;
    float distance = sqrt(dx * dx + dy * dy);
    return distance <= circleRadius;
}

void GPUFluidSimulator::onMouseDown(int gridX, int gridY) {
    if (isInsideCircle(gridX, gridY)) {
        isDragging = true;
    }
}

void GPUFluidSimulator::onMouseDrag(int gridX, int gridY) {
    if (isDragging) {
        // Clamp circle to bounds
        int newX = std::max(circleRadius, std::min(gridX, this->gridX - circleRadius - 1));
        int newY = std::max(circleRadius, std::min(gridY, this->gridY - circleRadius - 1));

        if (newX != circleX || newY != circleY) {
            moveCircle(newX, newY);
        }
    }
}

void GPUFluidSimulator::onMouseUp() {
    isDragging = false;
}

void GPUFluidSimulator::moveCircle(int newGridX, int newGridY) {
    prevCircleX = circleX;
    prevCircleY = circleY;

    // Calculate instantaneous velocity
    float timeStep = config->simulation.timestep;
    float instantVelX = (newGridX - circleX) / timeStep;
    float instantVelY = (newGridY - circleY) / timeStep;

    // Apply velocity smoothing (alpha = 0.3f, matching CPU implementation)
    float alpha = 0.3f;
    circleVelX = alpha * instantVelX + (1.0f - alpha) * circleVelX;
    circleVelY = alpha * instantVelY + (1.0f - alpha) * circleVelY;

    circleX = newGridX;
    circleY = newGridY;

    // Mark that circle was moved this frame (for momentum transfer)
    circleWasMoved = true;

    // Note: dispatchCircle() will be called from update() method
}

// holy boilerplate
bool GPUFluidSimulator::initGPUResources() {
    if (!device || !queue) {
        std::cerr << "Error: WebGPU device or queue is null in initGPUResources" << std::endl;
        return false;
    }

    if (!initTextures()) {
        return false;
    }
    if (!initSamplers()) {
        return false;
    }
    if (!initUniformBuffer()) {
        return false;
    }
    if (!initPipelineLayouts()) {
        return false;
    }
    if (!initBindGroups()) {
        return false;
    }
    if (!createIntegratePipeline()) {
        return false;
    }
    if (!createProjectionPipelines()) {
        return false;
    }
    if (!createExtrapolatePipeline()) {
        return false;
    }
    if (!createAdvectPipelines()) {
        return false;
    }
    if (!createBoundaryPipeline()) {
        return false;
    }
    if (!createBoundaryNeighborsPipeline()) {
        return false;
    }
    if (!createVorticityPipelines()) {
        return false;
    }
    if (!createCirclePipeline()) {
        return false;
    }

    return true;
}

void GPUFluidSimulator::releaseGPUResources() {
    if (velocityBindGroup) {
        wgpuBindGroupRelease(velocityBindGroup);
        velocityBindGroup = nullptr;
    }
    if (pressureBindGroup) {
        wgpuBindGroupRelease(pressureBindGroup);
        pressureBindGroup = nullptr;
    }
    if (densityBindGroup) {
        wgpuBindGroupRelease(densityBindGroup);
        densityBindGroup = nullptr;
    }
    if (inkBindGroup) {
        wgpuBindGroupRelease(inkBindGroup);
        inkBindGroup = nullptr;
    }
    if (uniformBindGroup) {
        wgpuBindGroupRelease(uniformBindGroup);
        uniformBindGroup = nullptr;
    }

    if (velocityBindGroupLayout) {
        wgpuBindGroupLayoutRelease(velocityBindGroupLayout);
        velocityBindGroupLayout = nullptr;
    }
    if (pressureBindGroupLayout) {
        wgpuBindGroupLayoutRelease(pressureBindGroupLayout);
        pressureBindGroupLayout = nullptr;
    }
    if (densityBindGroupLayout) {
        wgpuBindGroupLayoutRelease(densityBindGroupLayout);
        densityBindGroupLayout = nullptr;
    }
    if (inkBindGroupLayout) {
        wgpuBindGroupLayoutRelease(inkBindGroupLayout);
        inkBindGroupLayout = nullptr;
    }
    if (uniformBindGroupLayout) {
        wgpuBindGroupLayoutRelease(uniformBindGroupLayout);
        uniformBindGroupLayout = nullptr;
    }

    if (advectPipelineLayout) {
        wgpuPipelineLayoutRelease(advectPipelineLayout);
        advectPipelineLayout = nullptr;
    }

    if (integratePipelineLayout) {
        wgpuPipelineLayoutRelease(integratePipelineLayout);
        integratePipelineLayout = nullptr;
    }
    if (integrateBindGroupLayout) {
        wgpuBindGroupLayoutRelease(integrateBindGroupLayout);
        integrateBindGroupLayout = nullptr;
    }
    if (integrateBindGroup) {
        wgpuBindGroupRelease(integrateBindGroup);
        integrateBindGroup = nullptr;
    }
    if (integratePipelineLayout) {
        wgpuPipelineLayoutRelease(integratePipelineLayout);
        integratePipelineLayout = nullptr;
    }
    if (integratePipeline) {
        wgpuComputePipelineRelease(integratePipeline);
        integratePipeline = nullptr;
    }

    // Release projection stage resources
    if (divergenceBindGroup) {
        wgpuBindGroupRelease(divergenceBindGroup);
        divergenceBindGroup = nullptr;
    }
    if (divergenceBindGroupLayout) {
        wgpuBindGroupLayoutRelease(divergenceBindGroupLayout);
        divergenceBindGroupLayout = nullptr;
    }
    if (divergencePipelineLayout) {
        wgpuPipelineLayoutRelease(divergencePipelineLayout);
        divergencePipelineLayout = nullptr;
    }
    if (divergencePipeline) {
        wgpuComputePipelineRelease(divergencePipeline);
        divergencePipeline = nullptr;
    }
    if (jacobiPressurePipeline) {
        wgpuComputePipelineRelease(jacobiPressurePipeline);
        jacobiPressurePipeline = nullptr;
    }
    if (jacobiBindGroup) {
        wgpuBindGroupRelease(jacobiBindGroup);
        jacobiBindGroup = nullptr;
    }
    if (jacobiPingPongBindGroup) {
        wgpuBindGroupRelease(jacobiPingPongBindGroup);
        jacobiPingPongBindGroup = nullptr;
    }
    if (jacobiBindGroupLayout) {
        wgpuBindGroupLayoutRelease(jacobiBindGroupLayout);
        jacobiBindGroupLayout = nullptr;
    }
    if (jacobiPipelineLayout) {
        wgpuPipelineLayoutRelease(jacobiPipelineLayout);
        jacobiPipelineLayout = nullptr;
    }
    if (velocityUpdatePipeline) {
        wgpuComputePipelineRelease(velocityUpdatePipeline);
        velocityUpdatePipeline = nullptr;
    }
    if (velocityUpdateBindGroup) {
        wgpuBindGroupRelease(velocityUpdateBindGroup);
        velocityUpdateBindGroup = nullptr;
    }
    if (velocityUpdateBindGroupLayout) {
        wgpuBindGroupLayoutRelease(velocityUpdateBindGroupLayout);
        velocityUpdateBindGroupLayout = nullptr;
    }
    if (velocityUpdatePipelineLayout) {
        wgpuPipelineLayoutRelease(velocityUpdatePipelineLayout);
        velocityUpdatePipelineLayout = nullptr;
    }
    if (extrapolateBindGroup) {
        wgpuBindGroupRelease(extrapolateBindGroup);
        extrapolateBindGroup = nullptr;
    }
    if (extrapolateBindGroupLayout) {
        wgpuBindGroupLayoutRelease(extrapolateBindGroupLayout);
        extrapolateBindGroupLayout = nullptr;
    }
    if (extrapolatePipelineLayout) {
        wgpuPipelineLayoutRelease(extrapolatePipelineLayout);
        extrapolatePipelineLayout = nullptr;
    }
    if (extrapolatePipeline) {
        wgpuComputePipelineRelease(extrapolatePipeline);
        extrapolatePipeline = nullptr;
    }

    // Release advect resources
    if (advectVelocityBindGroup) {
        wgpuBindGroupRelease(advectVelocityBindGroup);
        advectVelocityBindGroup = nullptr;
    }
    if (advectDensityBindGroup) {
        wgpuBindGroupRelease(advectDensityBindGroup);
        advectDensityBindGroup = nullptr;
    }
    if (advectInkBindGroup) {
        wgpuBindGroupRelease(advectInkBindGroup);
        advectInkBindGroup = nullptr;
    }
    if (advectVelocityBindGroupLayout) {
        wgpuBindGroupLayoutRelease(advectVelocityBindGroupLayout);
        advectVelocityBindGroupLayout = nullptr;
    }
    if (advectDensityBindGroupLayout) {
        wgpuBindGroupLayoutRelease(advectDensityBindGroupLayout);
        advectDensityBindGroupLayout = nullptr;
    }
    if (advectInkBindGroupLayout) {
        wgpuBindGroupLayoutRelease(advectInkBindGroupLayout);
        advectInkBindGroupLayout = nullptr;
    }
    if (advectVelocityPipelineLayout) {
        wgpuPipelineLayoutRelease(advectVelocityPipelineLayout);
        advectVelocityPipelineLayout = nullptr;
    }
    if (advectDensityPipelineLayout) {
        wgpuPipelineLayoutRelease(advectDensityPipelineLayout);
        advectDensityPipelineLayout = nullptr;
    }
    if (advectInkPipelineLayout) {
        wgpuPipelineLayoutRelease(advectInkPipelineLayout);
        advectInkPipelineLayout = nullptr;
    }
    if (advectVelocityPipeline) {
        wgpuComputePipelineRelease(advectVelocityPipeline);
        advectVelocityPipeline = nullptr;
    }
    if (advectDensityPipeline) {
        wgpuComputePipelineRelease(advectDensityPipeline);
        advectDensityPipeline = nullptr;
    }
    if (advectInkPipeline) {
        wgpuComputePipelineRelease(advectInkPipeline);
        advectInkPipeline = nullptr;
    }

    // Release boundary resources
    if (boundaryBindGroup) {
        wgpuBindGroupRelease(boundaryBindGroup);
        boundaryBindGroup = nullptr;
    }
    if (boundaryBindGroupLayout) {
        wgpuBindGroupLayoutRelease(boundaryBindGroupLayout);
        boundaryBindGroupLayout = nullptr;
    }
    if (boundaryPipelineLayout) {
        wgpuPipelineLayoutRelease(boundaryPipelineLayout);
        boundaryPipelineLayout = nullptr;
    }
    if (boundaryPipeline) {
        wgpuComputePipelineRelease(boundaryPipeline);
        boundaryPipeline = nullptr;
    }

    // Release boundary neighbors resources
    if (boundaryNeighborsBindGroup) {
        wgpuBindGroupRelease(boundaryNeighborsBindGroup);
        boundaryNeighborsBindGroup = nullptr;
    }
    if (boundaryNeighborsBindGroupLayout) {
        wgpuBindGroupLayoutRelease(boundaryNeighborsBindGroupLayout);
        boundaryNeighborsBindGroupLayout = nullptr;
    }
    if (boundaryNeighborsPipelineLayout) {
        wgpuPipelineLayoutRelease(boundaryNeighborsPipelineLayout);
        boundaryNeighborsPipelineLayout = nullptr;
    }
    if (boundaryNeighborsPipeline) {
        wgpuComputePipelineRelease(boundaryNeighborsPipeline);
        boundaryNeighborsPipeline = nullptr;
    }

    // Release vorticity resources
    if (vorticityComputePipeline) {
        wgpuComputePipelineRelease(vorticityComputePipeline);
        vorticityComputePipeline = nullptr;
    }
    if (vorticityApplyPipeline) {
        wgpuComputePipelineRelease(vorticityApplyPipeline);
        vorticityApplyPipeline = nullptr;
    }
    if (vorticityComputeBindGroup) {
        wgpuBindGroupRelease(vorticityComputeBindGroup);
        vorticityComputeBindGroup = nullptr;
    }
    if (vorticityApplyBindGroup) {
        wgpuBindGroupRelease(vorticityApplyBindGroup);
        vorticityApplyBindGroup = nullptr;
    }
    if (vorticityComputeBindGroupLayout) {
        wgpuBindGroupLayoutRelease(vorticityComputeBindGroupLayout);
        vorticityComputeBindGroupLayout = nullptr;
    }
    if (vorticityApplyBindGroupLayout) {
        wgpuBindGroupLayoutRelease(vorticityApplyBindGroupLayout);
        vorticityApplyBindGroupLayout = nullptr;
    }
    if (vorticityComputePipelineLayout) {
        wgpuPipelineLayoutRelease(vorticityComputePipelineLayout);
        vorticityComputePipelineLayout = nullptr;
    }
    if (vorticityApplyPipelineLayout) {
        wgpuPipelineLayoutRelease(vorticityApplyPipelineLayout);
        vorticityApplyPipelineLayout = nullptr;
    }
    if (curlTextureView) {
        wgpuTextureViewRelease(curlTextureView);
        curlTextureView = nullptr;
    }
    if (curlTexture) {
        wgpuTextureRelease(curlTexture);
        curlTexture = nullptr;
    }

    // Circle resources
    if (circleBindGroup) {
        wgpuBindGroupRelease(circleBindGroup);
        circleBindGroup = nullptr;
    }
    if (circleBindGroupLayout) {
        wgpuBindGroupLayoutRelease(circleBindGroupLayout);
        circleBindGroupLayout = nullptr;
    }
    if (circlePipelineLayout) {
        wgpuPipelineLayoutRelease(circlePipelineLayout);
        circlePipelineLayout = nullptr;
    }
    if (circlePipeline) {
        wgpuComputePipelineRelease(circlePipeline);
        circlePipeline = nullptr;
    }

    if (uniformBuffer) {
        wgpuBufferRelease(uniformBuffer);
        uniformBuffer = nullptr;
    }

    if (sampler) {
        wgpuSamplerRelease(sampler);
        sampler = nullptr;
    }

    if (velocityTextureView) {
        wgpuTextureViewRelease(velocityTextureView);
        velocityTextureView = nullptr;
    }
    if (pressureTextureView) {
        wgpuTextureViewRelease(pressureTextureView);
        pressureTextureView = nullptr;
    }
    if (densityTextureView) {
        wgpuTextureViewRelease(densityTextureView);
        densityTextureView = nullptr;
    }
    if (solidTextureView) {
        wgpuTextureViewRelease(solidTextureView);
        solidTextureView = nullptr;
    }
    if (inkTextureView) {
        wgpuTextureViewRelease(inkTextureView);
        inkTextureView = nullptr;
    }
        if (newVelocityTextureView) {
        wgpuTextureViewRelease(newVelocityTextureView);
        newVelocityTextureView = nullptr;
    }
    if (newDensityTextureView) {
        wgpuTextureViewRelease(newDensityTextureView);
        newDensityTextureView = nullptr;
    }
    if (newInkTextureView) {
        wgpuTextureViewRelease(newInkTextureView);
        newInkTextureView = nullptr;
    }
    if (newPressureTextureView) {
        wgpuTextureViewRelease(newPressureTextureView);
        newPressureTextureView = nullptr;
    }

    if (velocityTexture) {
        wgpuTextureRelease(velocityTexture);
        velocityTexture = nullptr;
    }
    if (pressureTexture) {
        wgpuTextureRelease(pressureTexture);
        pressureTexture = nullptr;
    }
    if (densityTexture) {
        wgpuTextureRelease(densityTexture);
        densityTexture = nullptr;
    }
    if (solidTexture) {
        wgpuTextureRelease(solidTexture);
        solidTexture = nullptr;
    }
    if (inkTexture) {
        wgpuTextureRelease(inkTexture);
        inkTexture = nullptr;
    }
        if (newVelocityTexture) {
        wgpuTextureRelease(newVelocityTexture);
        newVelocityTexture = nullptr;
    }
    if (newDensityTexture) {
        wgpuTextureRelease(newDensityTexture);
        newDensityTexture = nullptr;
    }
    if (newInkTexture) {
        wgpuTextureRelease(newInkTexture);
        newInkTexture = nullptr;
    }
    if (newPressureTexture) {
        wgpuTextureRelease(newPressureTexture);
        newPressureTexture = nullptr;
    }
    if (curlTexture) {
        wgpuTextureRelease(curlTexture);
        curlTexture = nullptr;
    }

    webgpuInitialized = false;
}

bool GPUFluidSimulator::initTextures() {
    WGPUTextureDescriptor velocityDesc = {};
    velocityDesc.size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    velocityDesc.format = WGPUTextureFormat_RG32Float;
    velocityDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    velocityDesc.dimension = WGPUTextureDimension_2D;
    velocityDesc.mipLevelCount = 1;
    velocityDesc.sampleCount = 1;
    velocityTexture = wgpuDeviceCreateTexture(device, &velocityDesc);
    if (!velocityTexture) {
        std::cerr << "Failed to create velocity texture" << std::endl;
        return false;
    }

    WGPUTextureDescriptor pressureDesc = {};
    pressureDesc.size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    pressureDesc.format = WGPUTextureFormat_R32Float;
    pressureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    pressureDesc.dimension = WGPUTextureDimension_2D;
    pressureDesc.mipLevelCount = 1;
    pressureDesc.sampleCount = 1;
    pressureTexture = wgpuDeviceCreateTexture(device, &pressureDesc);
    if (!pressureTexture) {
        std::cerr << "Failed to create pressure texture" << std::endl;
        return false;
    }

    newPressureTexture = wgpuDeviceCreateTexture(device, &pressureDesc);
    if (!newPressureTexture) {
        std::cerr << "Failed to create new pressure texture" << std::endl;
        return false;
    }

    WGPUTextureDescriptor densityDesc = {};
    densityDesc.size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    densityDesc.format = WGPUTextureFormat_R32Float;
    densityDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    densityDesc.dimension = WGPUTextureDimension_2D;
    densityDesc.mipLevelCount = 1;
    densityDesc.sampleCount = 1;
    densityTexture = wgpuDeviceCreateTexture(device, &densityDesc);
    if (!densityTexture) {
        std::cerr << "Failed to create density texture" << std::endl;
        return false;
    }

    WGPUTextureDescriptor solidDesc = {};
    solidDesc.size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    solidDesc.format = WGPUTextureFormat_R32Float;
    solidDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    solidDesc.dimension = WGPUTextureDimension_2D;
    solidDesc.mipLevelCount = 1;
    solidDesc.sampleCount = 1;
    solidTexture = wgpuDeviceCreateTexture(device, &solidDesc);
    if (!solidTexture) {
        std::cerr << "Failed to create solid texture" << std::endl;
        return false;
    }

    WGPUTextureDescriptor inkDesc = {};
    inkDesc.size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    inkDesc.format = WGPUTextureFormat_RGBA32Float;
    inkDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    inkDesc.dimension = WGPUTextureDimension_2D;
    inkDesc.mipLevelCount = 1;
    inkDesc.sampleCount = 1;
    inkTexture = wgpuDeviceCreateTexture(device, &inkDesc);
    if (!inkTexture) {
        std::cerr << "Failed to create ink texture" << std::endl;
        return false;
    }

    // Create divergence texture
    WGPUTextureDescriptor divergenceDesc = {};
    divergenceDesc.size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    divergenceDesc.format = WGPUTextureFormat_R32Float;
    divergenceDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    divergenceDesc.dimension = WGPUTextureDimension_2D;
    divergenceDesc.mipLevelCount = 1;
    divergenceDesc.sampleCount = 1;
    divergenceTexture = wgpuDeviceCreateTexture(device, &divergenceDesc);
    if (!divergenceTexture) {
        std::cerr << "Failed to create divergence texture" << std::endl;
        return false;
    }

    newVelocityTexture = wgpuDeviceCreateTexture(device, &velocityDesc);
    if (!newVelocityTexture) {
        std::cerr << "Failed to create new velocity texture" << std::endl;
        return false;
    }

    newDensityTexture = wgpuDeviceCreateTexture(device, &densityDesc);
    if (!newDensityTexture) {
        std::cerr << "Failed to create new density texture" << std::endl;
        return false;
    }

    newInkTexture = wgpuDeviceCreateTexture(device, &inkDesc);
    if (!newInkTexture) {
        std::cerr << "Failed to create new ink texture" << std::endl;
        return false;
    }

    // Create curl texture for vorticity computation
    WGPUTextureDescriptor curlDesc = {};
    curlDesc.size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    curlDesc.format = WGPUTextureFormat_R32Float;
    curlDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    curlDesc.dimension = WGPUTextureDimension_2D;
    curlDesc.mipLevelCount = 1;
    curlDesc.sampleCount = 1;
    curlTexture = wgpuDeviceCreateTexture(device, &curlDesc);
    if (!curlTexture) {
        std::cerr << "Failed to create curl texture" << std::endl;
        return false;
    }

    velocityTextureView = wgpuTextureCreateView(velocityTexture, nullptr);
    pressureTextureView = wgpuTextureCreateView(pressureTexture, nullptr);
    densityTextureView = wgpuTextureCreateView(densityTexture, nullptr);
    solidTextureView = wgpuTextureCreateView(solidTexture, nullptr);
    inkTextureView = wgpuTextureCreateView(inkTexture, nullptr);
    divergenceTextureView = wgpuTextureCreateView(divergenceTexture, nullptr);
    newVelocityTextureView = wgpuTextureCreateView(newVelocityTexture, nullptr);
    newDensityTextureView = wgpuTextureCreateView(newDensityTexture, nullptr);
    newInkTextureView = wgpuTextureCreateView(newInkTexture, nullptr);
    newPressureTextureView = wgpuTextureCreateView(newPressureTexture, nullptr);
    curlTextureView = wgpuTextureCreateView(curlTexture, nullptr);

    return true;
}

bool GPUFluidSimulator::initSamplers() {
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.nextInChain = nullptr;
    samplerDesc.label = nullptr;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.lodMinClamp = 0.0f;
    samplerDesc.lodMaxClamp = 32.0f;
    samplerDesc.compare = WGPUCompareFunction_Undefined;
    samplerDesc.maxAnisotropy = 1;

    sampler = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!sampler) {
        std::cerr << "Failed to create sampler" << std::endl;
        return false;
    }

    return true;
}

struct alignas(16) SimParams { // 16-byte alignment
    int gridX;
    int gridY;
    float cellSize;
    float timeStep;
    float gravity;
    float vorticity;
    float vorticityLen;
    float projectionIters;
    float overrelaxationCoeff;
    float density;  // fluid density for pressure calculations
    int windTunnelSide;
    int windTunnelStart;
    int windTunnelEnd;
    float windTunnelVelocity;
    int circleX;
    int circleY;
    int prevCircleX;
    int prevCircleY;
    int circleRadius;
    float circleVelX;
    float circleVelY;
    float momentumTransferCoeff;
    float momentumTransferRadius;
    int circleWasMoved;
    float pad0;
    float pad1;
};

bool GPUFluidSimulator::initUniformBuffer() {
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.nextInChain = nullptr;
    uniformDesc.label = nullptr;
    uniformDesc.size = sizeof(SimParams);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformDesc.mappedAtCreation = false;

    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);
    if (!uniformBuffer) {
        std::cerr << "Failed to create uniform buffer" << std::endl;
        return false;
    }

    return true;
}

bool GPUFluidSimulator::initPipelineLayouts() {
    // integration pipeline
    WGPUBindGroupLayoutEntry integrationEntries[4] = {};

    // uniform buffer
    integrationEntries[0] = {};
    integrationEntries[0].binding = 0;
    integrationEntries[0].visibility = WGPUShaderStage_Compute;
    integrationEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    integrationEntries[0].buffer.minBindingSize = 0;
    integrationEntries[0].buffer.hasDynamicOffset = false;

    // old velocity texture (read)
    integrationEntries[1] = {};
    integrationEntries[1].binding = 1;
    integrationEntries[1].visibility = WGPUShaderStage_Compute;
    integrationEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    integrationEntries[1].storageTexture.format = WGPUTextureFormat_RG32Float;
    integrationEntries[1].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    // new velocity texture (write)
    integrationEntries[2] = {};
    integrationEntries[2].binding = 2;
    integrationEntries[2].visibility = WGPUShaderStage_Compute;
    integrationEntries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    integrationEntries[2].storageTexture.format = WGPUTextureFormat_RG32Float;
    integrationEntries[2].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    // solid texture (read)
    integrationEntries[3] = {};
    integrationEntries[3].binding = 3;
    integrationEntries[3].visibility = WGPUShaderStage_Compute;
    integrationEntries[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    integrationEntries[3].texture.viewDimension = WGPUTextureViewDimension_2D;
    integrationEntries[3].texture.multisampled = false;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 4;
    layoutDesc.entries = integrationEntries;

    integrateBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
    if (!integrateBindGroupLayout) {
        std::cerr << "Failed to create integrate bind group layout" << std::endl;
        return false;
    }

    WGPUPipelineLayoutDescriptor pipelineDesc = {};
    pipelineDesc.bindGroupLayoutCount = 1;
    pipelineDesc.bindGroupLayouts = &integrateBindGroupLayout;

    integratePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineDesc);
    if (!integratePipelineLayout) {
        std::cerr << "Failed to create integrate pipeline layout" << std::endl;
        return false;
    }

    // Create bind group layouts for each projection stage

    // computeDivergence bind group layout
    WGPUBindGroupLayoutEntry divergenceEntries[4] = {};
    divergenceEntries[0] = {}; // uniform
    divergenceEntries[0].binding = 0;
    divergenceEntries[0].visibility = WGPUShaderStage_Compute;
    divergenceEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    divergenceEntries[1] = {}; // velocity (read)
    divergenceEntries[1].binding = 1;
    divergenceEntries[1].visibility = WGPUShaderStage_Compute;
    divergenceEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    divergenceEntries[1].storageTexture.format = WGPUTextureFormat_RG32Float;

    divergenceEntries[2] = {}; // divergence (write)
    divergenceEntries[2].binding = 2;
    divergenceEntries[2].visibility = WGPUShaderStage_Compute;
    divergenceEntries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    divergenceEntries[2].storageTexture.format = WGPUTextureFormat_R32Float;

    divergenceEntries[3] = {}; // solid (read)
    divergenceEntries[3].binding = 3;
    divergenceEntries[3].visibility = WGPUShaderStage_Compute;
    divergenceEntries[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;

    WGPUBindGroupLayoutDescriptor divergenceLayoutDesc = {};
    divergenceLayoutDesc.entryCount = 4;
    divergenceLayoutDesc.entries = divergenceEntries;
    divergenceBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &divergenceLayoutDesc);
    if (!divergenceBindGroupLayout) {
        std::cerr << "Failed to create divergence bind group layout" << std::endl;
        return false;
    }

    // jacobiPressure bind group layout
    WGPUBindGroupLayoutEntry jacobiEntries[5] = {};
    jacobiEntries[0] = {}; // uniform
    jacobiEntries[0].binding = 0;
    jacobiEntries[0].visibility = WGPUShaderStage_Compute;
    jacobiEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    jacobiEntries[1] = {}; // divergence (read)
    jacobiEntries[1].binding = 1;
    jacobiEntries[1].visibility = WGPUShaderStage_Compute;
    jacobiEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    jacobiEntries[1].storageTexture.format = WGPUTextureFormat_R32Float;

    jacobiEntries[2] = {}; // pressure (read)
    jacobiEntries[2].binding = 2;
    jacobiEntries[2].visibility = WGPUShaderStage_Compute;
    jacobiEntries[2].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    jacobiEntries[2].storageTexture.format = WGPUTextureFormat_R32Float;

    jacobiEntries[3] = {}; // new pressure (write)
    jacobiEntries[3].binding = 3;
    jacobiEntries[3].visibility = WGPUShaderStage_Compute;
    jacobiEntries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    jacobiEntries[3].storageTexture.format = WGPUTextureFormat_R32Float;

    jacobiEntries[4] = {}; // solid (read)
    jacobiEntries[4].binding = 4;
    jacobiEntries[4].visibility = WGPUShaderStage_Compute;
    jacobiEntries[4].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;

    WGPUBindGroupLayoutDescriptor jacobiLayoutDesc = {};
    jacobiLayoutDesc.entryCount = 5;
    jacobiLayoutDesc.entries = jacobiEntries;
    jacobiBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &jacobiLayoutDesc);
    if (!jacobiBindGroupLayout) {
        std::cerr << "Failed to create jacobi bind group layout" << std::endl;
        return false;
    }

    // updateVelocityFromPressure bind group layout
    WGPUBindGroupLayoutEntry velocityUpdateEntries[5] = {};
    velocityUpdateEntries[0] = {}; // uniform
    velocityUpdateEntries[0].binding = 0;
    velocityUpdateEntries[0].visibility = WGPUShaderStage_Compute;
    velocityUpdateEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    velocityUpdateEntries[1] = {}; // velocity (read)
    velocityUpdateEntries[1].binding = 1;
    velocityUpdateEntries[1].visibility = WGPUShaderStage_Compute;
    velocityUpdateEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    velocityUpdateEntries[1].storageTexture.format = WGPUTextureFormat_RG32Float;

    velocityUpdateEntries[2] = {}; // new velocity (write)
    velocityUpdateEntries[2].binding = 2;
    velocityUpdateEntries[2].visibility = WGPUShaderStage_Compute;
    velocityUpdateEntries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    velocityUpdateEntries[2].storageTexture.format = WGPUTextureFormat_RG32Float;

    velocityUpdateEntries[3] = {}; // solid (read)
    velocityUpdateEntries[3].binding = 3;
    velocityUpdateEntries[3].visibility = WGPUShaderStage_Compute;
    velocityUpdateEntries[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;

    velocityUpdateEntries[4] = {}; // pressure (read)
    velocityUpdateEntries[4].binding = 4;
    velocityUpdateEntries[4].visibility = WGPUShaderStage_Compute;
    velocityUpdateEntries[4].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    velocityUpdateEntries[4].storageTexture.format = WGPUTextureFormat_R32Float;

    WGPUBindGroupLayoutDescriptor velocityUpdateLayoutDesc = {};
    velocityUpdateLayoutDesc.entryCount = 5;
    velocityUpdateLayoutDesc.entries = velocityUpdateEntries;
    velocityUpdateBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &velocityUpdateLayoutDesc);
    if (!velocityUpdateBindGroupLayout) {
        std::cerr << "Failed to create velocity update bind group layout" << std::endl;
        return false;
    }

    // extrapolate bind group layout
    WGPUBindGroupLayoutEntry extrapolateEntries[3] = {};
    extrapolateEntries[0] = {}; // uniform buffer
    extrapolateEntries[0].binding = 0;
    extrapolateEntries[0].visibility = WGPUShaderStage_Compute;
    extrapolateEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    extrapolateEntries[1] = {}; // velocity texture (read)
    extrapolateEntries[1].binding = 1;
    extrapolateEntries[1].visibility = WGPUShaderStage_Compute;
    extrapolateEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    extrapolateEntries[1].storageTexture.format = WGPUTextureFormat_RG32Float;

    extrapolateEntries[2] = {}; // new velocity texture (write)
    extrapolateEntries[2].binding = 2;
    extrapolateEntries[2].visibility = WGPUShaderStage_Compute;
    extrapolateEntries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    extrapolateEntries[2].storageTexture.format = WGPUTextureFormat_RG32Float;

    WGPUBindGroupLayoutDescriptor extrapolateLayoutDesc = {};
    extrapolateLayoutDesc.entryCount = 3;
    extrapolateLayoutDesc.entries = extrapolateEntries;

    extrapolateBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &extrapolateLayoutDesc);
    if (!extrapolateBindGroupLayout) {
        std::cerr << "Failed to create extrapolate bind group layout" << std::endl;
        return false;
    }

    // Create pipeline layouts from bind group layouts
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};

    // divergence pipeline layout
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &divergenceBindGroupLayout;
    divergencePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);
    if (!divergencePipelineLayout) {
        std::cerr << "Failed to create divergence pipeline layout" << std::endl;
        return false;
    }

    // jacobi pipeline layout
    pipelineLayoutDesc.bindGroupLayouts = &jacobiBindGroupLayout;
    jacobiPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);
    if (!jacobiPipelineLayout) {
        std::cerr << "Failed to create jacobi pipeline layout" << std::endl;
        return false;
    }

    // velocity update pipeline layout
    pipelineLayoutDesc.bindGroupLayouts = &velocityUpdateBindGroupLayout;
    velocityUpdatePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);
    if (!velocityUpdatePipelineLayout) {
        std::cerr << "Failed to create velocity update pipeline layout" << std::endl;
        return false;
    }

    // extrapolate pipeline layout
    pipelineLayoutDesc.bindGroupLayouts = &extrapolateBindGroupLayout;
    extrapolatePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);
    if (!extrapolatePipelineLayout) {
        std::cerr << "Failed to create extrapolate pipeline layout" << std::endl;
        return false;
    }

    // advect velocity bind group layout
    WGPUBindGroupLayoutEntry advectVelocityEntries[4] = {};
    advectVelocityEntries[0] = {}; // uniform buffer
    advectVelocityEntries[0].binding = 0;
    advectVelocityEntries[0].visibility = WGPUShaderStage_Compute;
    advectVelocityEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    advectVelocityEntries[1] = {}; // velocity texture (read)
    advectVelocityEntries[1].binding = 1;
    advectVelocityEntries[1].visibility = WGPUShaderStage_Compute;
    advectVelocityEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    advectVelocityEntries[1].storageTexture.format = WGPUTextureFormat_RG32Float;

    advectVelocityEntries[2] = {}; // solid texture (read)
    advectVelocityEntries[2].binding = 2;
    advectVelocityEntries[2].visibility = WGPUShaderStage_Compute;
    advectVelocityEntries[2].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;

    advectVelocityEntries[3] = {}; // new velocity texture (write)
    advectVelocityEntries[3].binding = 3;
    advectVelocityEntries[3].visibility = WGPUShaderStage_Compute;
    advectVelocityEntries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    advectVelocityEntries[3].storageTexture.format = WGPUTextureFormat_RG32Float;

    WGPUBindGroupLayoutDescriptor advectVelocityLayoutDesc = {};
    advectVelocityLayoutDesc.entryCount = 4;
    advectVelocityLayoutDesc.entries = advectVelocityEntries;
    advectVelocityBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &advectVelocityLayoutDesc);
    if (!advectVelocityBindGroupLayout) {
        std::cerr << "Failed to create advect velocity bind group layout" << std::endl;
        return false;
    }

    // advect density bind group layout
    WGPUBindGroupLayoutEntry advectDensityEntries[5] = {};
    advectDensityEntries[0] = {}; // uniform buffer
    advectDensityEntries[0].binding = 0;
    advectDensityEntries[0].visibility = WGPUShaderStage_Compute;
    advectDensityEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    advectDensityEntries[1] = {}; // velocity texture (read)
    advectDensityEntries[1].binding = 1;
    advectDensityEntries[1].visibility = WGPUShaderStage_Compute;
    advectDensityEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    advectDensityEntries[1].storageTexture.format = WGPUTextureFormat_RG32Float;

    advectDensityEntries[2] = {}; // density texture (read)
    advectDensityEntries[2].binding = 2;
    advectDensityEntries[2].visibility = WGPUShaderStage_Compute;
    advectDensityEntries[2].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    advectDensityEntries[2].storageTexture.format = WGPUTextureFormat_R32Float;

    advectDensityEntries[3] = {}; // solid texture (read)
    advectDensityEntries[3].binding = 3;
    advectDensityEntries[3].visibility = WGPUShaderStage_Compute;
    advectDensityEntries[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;

    advectDensityEntries[4] = {}; // new density texture (write)
    advectDensityEntries[4].binding = 4;
    advectDensityEntries[4].visibility = WGPUShaderStage_Compute;
    advectDensityEntries[4].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    advectDensityEntries[4].storageTexture.format = WGPUTextureFormat_R32Float;

    WGPUBindGroupLayoutDescriptor advectDensityLayoutDesc = {};
    advectDensityLayoutDesc.entryCount = 5;
    advectDensityLayoutDesc.entries = advectDensityEntries;
    advectDensityBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &advectDensityLayoutDesc);
    if (!advectDensityBindGroupLayout) {
        std::cerr << "Failed to create advect density bind group layout" << std::endl;
        return false;
    }

    // advect ink bind group layout
    WGPUBindGroupLayoutEntry advectInkEntries[5] = {};
    advectInkEntries[0] = {}; // uniform buffer
    advectInkEntries[0].binding = 0;
    advectInkEntries[0].visibility = WGPUShaderStage_Compute;
    advectInkEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    advectInkEntries[1] = {}; // velocity texture (read)
    advectInkEntries[1].binding = 1;
    advectInkEntries[1].visibility = WGPUShaderStage_Compute;
    advectInkEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    advectInkEntries[1].storageTexture.format = WGPUTextureFormat_RG32Float;

    advectInkEntries[2] = {}; // ink texture (read)
    advectInkEntries[2].binding = 2;
    advectInkEntries[2].visibility = WGPUShaderStage_Compute;
    advectInkEntries[2].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    advectInkEntries[2].storageTexture.format = WGPUTextureFormat_RGBA32Float;

    advectInkEntries[3] = {}; // solid texture (read)
    advectInkEntries[3].binding = 3;
    advectInkEntries[3].visibility = WGPUShaderStage_Compute;
    advectInkEntries[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;

    advectInkEntries[4] = {}; // new ink texture (write)
    advectInkEntries[4].binding = 4;
    advectInkEntries[4].visibility = WGPUShaderStage_Compute;
    advectInkEntries[4].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    advectInkEntries[4].storageTexture.format = WGPUTextureFormat_RGBA32Float;

    WGPUBindGroupLayoutDescriptor advectInkLayoutDesc = {};
    advectInkLayoutDesc.entryCount = 5;
    advectInkLayoutDesc.entries = advectInkEntries;
    advectInkBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &advectInkLayoutDesc);
    if (!advectInkBindGroupLayout) {
        std::cerr << "Failed to create advect ink bind group layout" << std::endl;
        return false;
    }

    // Create advect pipeline layouts
    WGPUPipelineLayoutDescriptor advectPipelineLayoutDesc = pipelineLayoutDesc;
    advectPipelineLayoutDesc.bindGroupLayouts = &advectVelocityBindGroupLayout;
    advectVelocityPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &advectPipelineLayoutDesc);
    if (!advectVelocityPipelineLayout) {
        std::cerr << "Failed to create advect velocity pipeline layout" << std::endl;
        return false;
    }

    advectPipelineLayoutDesc.bindGroupLayouts = &advectDensityBindGroupLayout;
    advectDensityPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &advectPipelineLayoutDesc);
    if (!advectDensityPipelineLayout) {
        std::cerr << "Failed to create advect density pipeline layout" << std::endl;
        return false;
    }

    advectPipelineLayoutDesc.bindGroupLayouts = &advectInkBindGroupLayout;
    advectInkPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &advectPipelineLayoutDesc);
    if (!advectInkPipelineLayout) {
        std::cerr << "Failed to create advect ink pipeline layout" << std::endl;
        return false;
    }

    // boundary condition bind group layout
    WGPUBindGroupLayoutEntry boundaryEntries[6] = {};
    boundaryEntries[0] = {}; // uniform buffer
    boundaryEntries[0].binding = 0;
    boundaryEntries[0].visibility = WGPUShaderStage_Compute;
    boundaryEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    boundaryEntries[1] = {}; // velocity texture (read)
    boundaryEntries[1].binding = 1;
    boundaryEntries[1].visibility = WGPUShaderStage_Compute;
    boundaryEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    boundaryEntries[1].storageTexture.format = WGPUTextureFormat_RG32Float;

    boundaryEntries[2] = {}; // solid texture (read)
    boundaryEntries[2].binding = 2;
    boundaryEntries[2].visibility = WGPUShaderStage_Compute;
    boundaryEntries[2].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;

    boundaryEntries[3] = {}; // new velocity texture (write)
    boundaryEntries[3].binding = 3;
    boundaryEntries[3].visibility = WGPUShaderStage_Compute;
    boundaryEntries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    boundaryEntries[3].storageTexture.format = WGPUTextureFormat_RG32Float;

    boundaryEntries[4] = {}; // density texture (read)
    boundaryEntries[4].binding = 4;
    boundaryEntries[4].visibility = WGPUShaderStage_Compute;
    boundaryEntries[4].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    boundaryEntries[4].storageTexture.format = WGPUTextureFormat_R32Float;

    boundaryEntries[5] = {}; // new density texture (write)
    boundaryEntries[5].binding = 5;
    boundaryEntries[5].visibility = WGPUShaderStage_Compute;
    boundaryEntries[5].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    boundaryEntries[5].storageTexture.format = WGPUTextureFormat_R32Float;

    WGPUBindGroupLayoutDescriptor boundaryLayoutDesc = {};
    boundaryLayoutDesc.entryCount = 6;
    boundaryLayoutDesc.entries = boundaryEntries;
    boundaryBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &boundaryLayoutDesc);
    if (!boundaryBindGroupLayout) {
        std::cerr << "Failed to create boundary bind group layout" << std::endl;
        return false;
    }

    // Create boundary pipeline layout
    WGPUPipelineLayoutDescriptor boundaryPipelineLayoutDesc = {};
    boundaryPipelineLayoutDesc.bindGroupLayoutCount = 1;
    boundaryPipelineLayoutDesc.bindGroupLayouts = &boundaryBindGroupLayout;
    boundaryPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &boundaryPipelineLayoutDesc);
    if (!boundaryPipelineLayout) {
        std::cerr << "Failed to create boundary pipeline layout" << std::endl;
        return false;
    }

    // boundary neighbors bind group layout (for clearing velocity components adjacent to solids)
    WGPUBindGroupLayoutEntry boundaryNeighborsEntries[4] = {};
    boundaryNeighborsEntries[0] = {}; // uniform buffer
    boundaryNeighborsEntries[0].binding = 0;
    boundaryNeighborsEntries[0].visibility = WGPUShaderStage_Compute;
    boundaryNeighborsEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    boundaryNeighborsEntries[1] = {}; // velocity texture (read)
    boundaryNeighborsEntries[1].binding = 1;
    boundaryNeighborsEntries[1].visibility = WGPUShaderStage_Compute;
    boundaryNeighborsEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    boundaryNeighborsEntries[1].storageTexture.format = WGPUTextureFormat_RG32Float;

    boundaryNeighborsEntries[2] = {}; // solid texture (read)
    boundaryNeighborsEntries[2].binding = 2;
    boundaryNeighborsEntries[2].visibility = WGPUShaderStage_Compute;
    boundaryNeighborsEntries[2].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;

    boundaryNeighborsEntries[3] = {}; // new velocity texture (write)
    boundaryNeighborsEntries[3].binding = 3;
    boundaryNeighborsEntries[3].visibility = WGPUShaderStage_Compute;
    boundaryNeighborsEntries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    boundaryNeighborsEntries[3].storageTexture.format = WGPUTextureFormat_RG32Float;

    WGPUBindGroupLayoutDescriptor boundaryNeighborsLayoutDesc = {};
    boundaryNeighborsLayoutDesc.entryCount = 4;
    boundaryNeighborsLayoutDesc.entries = boundaryNeighborsEntries;
    boundaryNeighborsBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &boundaryNeighborsLayoutDesc);
    if (!boundaryNeighborsBindGroupLayout) {
        std::cerr << "Failed to create boundary neighbors bind group layout" << std::endl;
        return false;
    }

    // Create boundary neighbors pipeline layout
    WGPUPipelineLayoutDescriptor boundaryNeighborsPipelineLayoutDesc = {};
    boundaryNeighborsPipelineLayoutDesc.bindGroupLayoutCount = 1;
    boundaryNeighborsPipelineLayoutDesc.bindGroupLayouts = &boundaryNeighborsBindGroupLayout;
    boundaryNeighborsPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &boundaryNeighborsPipelineLayoutDesc);
    if (!boundaryNeighborsPipelineLayout) {
        std::cerr << "Failed to create boundary neighbors pipeline layout" << std::endl;
        return false;
    }

    // vorticity compute pipeline layout (for computing curl)
    WGPUBindGroupLayoutEntry vorticityComputeEntries[4] = {};
    vorticityComputeEntries[0] = {}; // uniform buffer
    vorticityComputeEntries[0].binding = 0;
    vorticityComputeEntries[0].visibility = WGPUShaderStage_Compute;
    vorticityComputeEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    vorticityComputeEntries[1] = {}; // velocity texture (read)
    vorticityComputeEntries[1].binding = 1;
    vorticityComputeEntries[1].visibility = WGPUShaderStage_Compute;
    vorticityComputeEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    vorticityComputeEntries[1].storageTexture.format = WGPUTextureFormat_RG32Float;

    vorticityComputeEntries[2] = {}; // solid texture (read)
    vorticityComputeEntries[2].binding = 2;
    vorticityComputeEntries[2].visibility = WGPUShaderStage_Compute;
    vorticityComputeEntries[2].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    vorticityComputeEntries[2].storageTexture.format = WGPUTextureFormat_R32Float;

    vorticityComputeEntries[3] = {}; // curl texture (write)
    vorticityComputeEntries[3].binding = 3;
    vorticityComputeEntries[3].visibility = WGPUShaderStage_Compute;
    vorticityComputeEntries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    vorticityComputeEntries[3].storageTexture.format = WGPUTextureFormat_R32Float;

    WGPUBindGroupLayoutDescriptor vorticityComputeLayoutDesc = {};
    vorticityComputeLayoutDesc.entryCount = 4;
    vorticityComputeLayoutDesc.entries = vorticityComputeEntries;

    vorticityComputeBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &vorticityComputeLayoutDesc);
    if (!vorticityComputeBindGroupLayout) {
        std::cerr << "Failed to create vorticity compute bind group layout" << std::endl;
        return false;
    }

    // vorticity apply pipeline layout (for applying forces)
    WGPUBindGroupLayoutEntry vorticityApplyEntries[5] = {};
    vorticityApplyEntries[0] = {}; // uniform buffer
    vorticityApplyEntries[0].binding = 0;
    vorticityApplyEntries[0].visibility = WGPUShaderStage_Compute;
    vorticityApplyEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    vorticityApplyEntries[1] = {}; // velocity texture (read)
    vorticityApplyEntries[1].binding = 1;
    vorticityApplyEntries[1].visibility = WGPUShaderStage_Compute;
    vorticityApplyEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    vorticityApplyEntries[1].storageTexture.format = WGPUTextureFormat_RG32Float;

    vorticityApplyEntries[2] = {}; // new velocity texture (write)
    vorticityApplyEntries[2].binding = 2;
    vorticityApplyEntries[2].visibility = WGPUShaderStage_Compute;
    vorticityApplyEntries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    vorticityApplyEntries[2].storageTexture.format = WGPUTextureFormat_RG32Float;

    vorticityApplyEntries[3] = {}; // solid texture (read)
    vorticityApplyEntries[3].binding = 3;
    vorticityApplyEntries[3].visibility = WGPUShaderStage_Compute;
    vorticityApplyEntries[3].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    vorticityApplyEntries[3].storageTexture.format = WGPUTextureFormat_R32Float;

    vorticityApplyEntries[4] = {}; // curl texture (read)
    vorticityApplyEntries[4].binding = 4;
    vorticityApplyEntries[4].visibility = WGPUShaderStage_Compute;
    vorticityApplyEntries[4].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    vorticityApplyEntries[4].storageTexture.format = WGPUTextureFormat_R32Float;

    WGPUBindGroupLayoutDescriptor vorticityApplyLayoutDesc = {};
    vorticityApplyLayoutDesc.entryCount = 5;
    vorticityApplyLayoutDesc.entries = vorticityApplyEntries;

    vorticityApplyBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &vorticityApplyLayoutDesc);
    if (!vorticityApplyBindGroupLayout) {
        std::cerr << "Failed to create vorticity apply bind group layout" << std::endl;
        return false;
    }

    // Create vorticity pipeline layouts
    WGPUPipelineLayoutDescriptor vorticityComputePipelineLayoutDesc = {};
    vorticityComputePipelineLayoutDesc.bindGroupLayoutCount = 1;
    vorticityComputePipelineLayoutDesc.bindGroupLayouts = &vorticityComputeBindGroupLayout;
    vorticityComputePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &vorticityComputePipelineLayoutDesc);
    if (!vorticityComputePipelineLayout) {
        std::cerr << "Failed to create vorticity compute pipeline layout" << std::endl;
        return false;
    }

    WGPUPipelineLayoutDescriptor vorticityApplyPipelineLayoutDesc = {};
    vorticityApplyPipelineLayoutDesc.bindGroupLayoutCount = 1;
    vorticityApplyPipelineLayoutDesc.bindGroupLayouts = &vorticityApplyBindGroupLayout;
    vorticityApplyPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &vorticityApplyPipelineLayoutDesc);
    if (!vorticityApplyPipelineLayout) {
        std::cerr << "Failed to create vorticity apply pipeline layout" << std::endl;
        return false;
    }

    // Create circle bind group layout
    WGPUBindGroupLayoutEntry circleLayoutEntries[5] = {};

    // uniform buffer
    circleLayoutEntries[0] = {};
    circleLayoutEntries[0].binding = 0;
    circleLayoutEntries[0].visibility = WGPUShaderStage_Compute;
    circleLayoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    circleLayoutEntries[0].buffer.minBindingSize = sizeof(SimParams);

    // solid texture (read-write)
    circleLayoutEntries[1] = {};
    circleLayoutEntries[1].binding = 1;
    circleLayoutEntries[1].visibility = WGPUShaderStage_Compute;
    circleLayoutEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
    circleLayoutEntries[1].storageTexture.format = WGPUTextureFormat_R32Float;
    circleLayoutEntries[1].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    // velocity texture (read-only)
    circleLayoutEntries[2] = {};
    circleLayoutEntries[2].binding = 2;
    circleLayoutEntries[2].visibility = WGPUShaderStage_Compute;
    circleLayoutEntries[2].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    circleLayoutEntries[2].storageTexture.format = WGPUTextureFormat_RG32Float;
    circleLayoutEntries[2].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    // new velocity texture (write-only)
    circleLayoutEntries[3] = {};
    circleLayoutEntries[3].binding = 3;
    circleLayoutEntries[3].visibility = WGPUShaderStage_Compute;
    circleLayoutEntries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    circleLayoutEntries[3].storageTexture.format = WGPUTextureFormat_RG32Float;
    circleLayoutEntries[3].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    // density texture (read-write)
    circleLayoutEntries[4] = {};
    circleLayoutEntries[4].binding = 4;
    circleLayoutEntries[4].visibility = WGPUShaderStage_Compute;
    circleLayoutEntries[4].storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
    circleLayoutEntries[4].storageTexture.format = WGPUTextureFormat_R32Float;
    circleLayoutEntries[4].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor circleLayoutDesc = {};
    circleLayoutDesc.label = "Circle Bind Group Layout";
    circleLayoutDesc.entryCount = 5;
    circleLayoutDesc.entries = circleLayoutEntries;

    circleBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &circleLayoutDesc);
    if (!circleBindGroupLayout) {
        std::cerr << "Failed to create circle bind group layout" << std::endl;
        return false;
    }

    // Create circle pipeline layout
    WGPUPipelineLayoutDescriptor circlePipelineLayoutDesc = {};
    circlePipelineLayoutDesc.bindGroupLayoutCount = 1;
    circlePipelineLayoutDesc.bindGroupLayouts = &circleBindGroupLayout;
    circlePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &circlePipelineLayoutDesc);
    if (!circlePipelineLayout) {
        std::cerr << "Failed to create circle pipeline layout" << std::endl;
        return false;
    }

    return true;
}

bool GPUFluidSimulator::initBindGroups() {
    WGPUBindGroupEntry integrateEntries[4] = {};

    // uniform buffer
    integrateEntries[0] = {};
    integrateEntries[0].binding = 0;
    integrateEntries[0].buffer = uniformBuffer;
    integrateEntries[0].offset = 0;
    integrateEntries[0].size = sizeof(SimParams);

    // old velocity texture (read)
    integrateEntries[1] = {};
    integrateEntries[1].binding = 1;
    integrateEntries[1].textureView = velocityTextureView;

    // new velocity texture (write)
    integrateEntries[2] = {};
    integrateEntries[2].binding = 2;
    integrateEntries[2].textureView = newVelocityTextureView;

    // solid texture (read)
    integrateEntries[3] = {};
    integrateEntries[3].binding = 3;
    integrateEntries[3].textureView = solidTextureView;

    WGPUBindGroupDescriptor integrateDesc = {};
    integrateDesc.layout = integrateBindGroupLayout;
    integrateDesc.entryCount = 4;
    integrateDesc.entries = integrateEntries;

    integrateBindGroup = wgpuDeviceCreateBindGroup(device, &integrateDesc);
    if (!integrateBindGroup) {
        std::cerr << "Failed to create integrate bind group" << std::endl;
        return false;
    }

    // Create divergence bind group (for computeDivergence)
    WGPUBindGroupEntry divergenceEntries[4] = {};
    divergenceEntries[0] = {}; // uniform
    divergenceEntries[0].binding = 0;
    divergenceEntries[0].buffer = uniformBuffer;
    divergenceEntries[0].offset = 0;
    divergenceEntries[0].size = sizeof(SimParams);

    divergenceEntries[1] = {}; // velocity (read)
    divergenceEntries[1].binding = 1;
    divergenceEntries[1].textureView = velocityTextureView;

    divergenceEntries[2] = {}; // divergence (write)
    divergenceEntries[2].binding = 2;
    divergenceEntries[2].textureView = divergenceTextureView;

    divergenceEntries[3] = {}; // solid (read)
    divergenceEntries[3].binding = 3;
    divergenceEntries[3].textureView = solidTextureView;

    WGPUBindGroupDescriptor divergenceDesc = {};
    divergenceDesc.layout = divergenceBindGroupLayout;
    divergenceDesc.entryCount = 4;
    divergenceDesc.entries = divergenceEntries;
    divergenceBindGroup = wgpuDeviceCreateBindGroup(device, &divergenceDesc);
    if (!divergenceBindGroup) {
        std::cerr << "Failed to create divergence bind group" << std::endl;
        return false;
    }

    // Create jacobi bind groups (ping-pong)
    WGPUBindGroupEntry jacobiEntries[5] = {};
    jacobiEntries[0] = {}; // uniform
    jacobiEntries[0].binding = 0;
    jacobiEntries[0].buffer = uniformBuffer;
    jacobiEntries[0].offset = 0;
    jacobiEntries[0].size = sizeof(SimParams);

    jacobiEntries[1] = {}; // divergence (read)
    jacobiEntries[1].binding = 1;
    jacobiEntries[1].textureView = divergenceTextureView;

    jacobiEntries[2] = {}; // pressure (read)
    jacobiEntries[2].binding = 2;
    jacobiEntries[2].textureView = pressureTextureView;

    jacobiEntries[3] = {}; // new pressure (write)
    jacobiEntries[3].binding = 3;
    jacobiEntries[3].textureView = newPressureTextureView;

    jacobiEntries[4] = {}; // solid (read)
    jacobiEntries[4].binding = 4;
    jacobiEntries[4].textureView = solidTextureView;

    WGPUBindGroupDescriptor jacobiDesc = {};
    jacobiDesc.layout = jacobiBindGroupLayout;
    jacobiDesc.entryCount = 5;
    jacobiDesc.entries = jacobiEntries;
    jacobiBindGroup = wgpuDeviceCreateBindGroup(device, &jacobiDesc);
    if (!jacobiBindGroup) {
        std::cerr << "Failed to create jacobi bind group" << std::endl;
        return false;
    }

    // ping-pong: read newPressure, write pressure
    jacobiEntries[2].textureView = newPressureTextureView;
    jacobiEntries[3].textureView = pressureTextureView;

    WGPUBindGroupDescriptor jacobiPingDesc = {};
    jacobiPingDesc.layout = jacobiBindGroupLayout;
    jacobiPingDesc.entryCount = 5;
    jacobiPingDesc.entries = jacobiEntries;
    jacobiPingPongBindGroup = wgpuDeviceCreateBindGroup(device, &jacobiPingDesc);
    if (!jacobiPingPongBindGroup) {
        std::cerr << "Failed to create jacobi ping-pong bind group" << std::endl;
        return false;
    }

    // Create velocity update bind group (for updateVelocityFromPressure)
    WGPUBindGroupEntry velocityUpdateEntries[5] = {};
    velocityUpdateEntries[0] = {}; // uniform
    velocityUpdateEntries[0].binding = 0;
    velocityUpdateEntries[0].buffer = uniformBuffer;
    velocityUpdateEntries[0].offset = 0;
    velocityUpdateEntries[0].size = sizeof(SimParams);

    velocityUpdateEntries[1] = {}; // velocity (read)
    velocityUpdateEntries[1].binding = 1;
    velocityUpdateEntries[1].textureView = velocityTextureView;

    velocityUpdateEntries[2] = {}; // new velocity (write)
    velocityUpdateEntries[2].binding = 2;
    velocityUpdateEntries[2].textureView = newVelocityTextureView;

    velocityUpdateEntries[3] = {}; // solid (read)
    velocityUpdateEntries[3].binding = 3;
    velocityUpdateEntries[3].textureView = solidTextureView;

    velocityUpdateEntries[4] = {}; // pressure (read)
    velocityUpdateEntries[4].binding = 4;
    velocityUpdateEntries[4].textureView = pressureTextureView;

    WGPUBindGroupDescriptor velocityUpdateDesc = {};
    velocityUpdateDesc.layout = velocityUpdateBindGroupLayout;
    velocityUpdateDesc.entryCount = 5;
    velocityUpdateDesc.entries = velocityUpdateEntries;
    velocityUpdateBindGroup = wgpuDeviceCreateBindGroup(device, &velocityUpdateDesc);
    if (!velocityUpdateBindGroup) {
        std::cerr << "Failed to create velocity update bind group" << std::endl;
        return false;
    }

    // extrapolate bind group
    WGPUBindGroupEntry extrapolateBindEntries[3] = {};
    extrapolateBindEntries[0] = {}; // uniform buffer
    extrapolateBindEntries[0].binding = 0;
    extrapolateBindEntries[0].buffer = uniformBuffer;
    extrapolateBindEntries[0].offset = 0;
    extrapolateBindEntries[0].size = sizeof(SimParams);

    extrapolateBindEntries[1] = {}; // velocity texture (read)
    extrapolateBindEntries[1].binding = 1;
    extrapolateBindEntries[1].textureView = velocityTextureView;

    extrapolateBindEntries[2] = {}; // new velocity texture (write)
    extrapolateBindEntries[2].binding = 2;
    extrapolateBindEntries[2].textureView = newVelocityTextureView;

    WGPUBindGroupDescriptor extrapolateBindDesc = {};
    extrapolateBindDesc.layout = extrapolateBindGroupLayout;
    extrapolateBindDesc.entryCount = 3;
    extrapolateBindDesc.entries = extrapolateBindEntries;

    extrapolateBindGroup = wgpuDeviceCreateBindGroup(device, &extrapolateBindDesc);
    if (!extrapolateBindGroup) {
        std::cerr << "Failed to create extrapolate bind group" << std::endl;
        return false;
    }

    // advect velocity bind group
    WGPUBindGroupEntry advectVelocityEntries[4] = {};
    advectVelocityEntries[0] = {}; // uniform buffer
    advectVelocityEntries[0].binding = 0;
    advectVelocityEntries[0].buffer = uniformBuffer;
    advectVelocityEntries[0].offset = 0;
    advectVelocityEntries[0].size = sizeof(SimParams);

    advectVelocityEntries[1] = {}; // velocity texture (read)
    advectVelocityEntries[1].binding = 1;
    advectVelocityEntries[1].textureView = velocityTextureView;

    advectVelocityEntries[2] = {}; // solid texture (read)
    advectVelocityEntries[2].binding = 2;
    advectVelocityEntries[2].textureView = solidTextureView;

    advectVelocityEntries[3] = {}; // new velocity texture (write)
    advectVelocityEntries[3].binding = 3;
    advectVelocityEntries[3].textureView = newVelocityTextureView;

    WGPUBindGroupDescriptor advectVelocityDesc = {};
    advectVelocityDesc.layout = advectVelocityBindGroupLayout;
    advectVelocityDesc.entryCount = 4;
    advectVelocityDesc.entries = advectVelocityEntries;
    advectVelocityBindGroup = wgpuDeviceCreateBindGroup(device, &advectVelocityDesc);
    if (!advectVelocityBindGroup) {
        std::cerr << "Failed to create advect velocity bind group" << std::endl;
        return false;
    }

    // advect density bind group
    WGPUBindGroupEntry advectDensityEntries[5] = {};
    advectDensityEntries[0] = {}; // uniform buffer
    advectDensityEntries[0].binding = 0;
    advectDensityEntries[0].buffer = uniformBuffer;
    advectDensityEntries[0].offset = 0;
    advectDensityEntries[0].size = sizeof(SimParams);

    advectDensityEntries[1] = {}; // velocity texture (read)
    advectDensityEntries[1].binding = 1;
    advectDensityEntries[1].textureView = velocityTextureView;

    advectDensityEntries[2] = {}; // density texture (read)
    advectDensityEntries[2].binding = 2;
    advectDensityEntries[2].textureView = densityTextureView;

    advectDensityEntries[3] = {}; // solid texture (read)
    advectDensityEntries[3].binding = 3;
    advectDensityEntries[3].textureView = solidTextureView;

    advectDensityEntries[4] = {}; // new density texture (write)
    advectDensityEntries[4].binding = 4;
    advectDensityEntries[4].textureView = newDensityTextureView;

    WGPUBindGroupDescriptor advectDensityDesc = {};
    advectDensityDesc.layout = advectDensityBindGroupLayout;
    advectDensityDesc.entryCount = 5;
    advectDensityDesc.entries = advectDensityEntries;
    advectDensityBindGroup = wgpuDeviceCreateBindGroup(device, &advectDensityDesc);
    if (!advectDensityBindGroup) {
        std::cerr << "Failed to create advect density bind group" << std::endl;
        return false;
    }

    // advect ink bind group
    WGPUBindGroupEntry advectInkEntries[5] = {};
    advectInkEntries[0] = {}; // uniform buffer
    advectInkEntries[0].binding = 0;
    advectInkEntries[0].buffer = uniformBuffer;
    advectInkEntries[0].offset = 0;
    advectInkEntries[0].size = sizeof(SimParams);

    advectInkEntries[1] = {}; // velocity texture (read)
    advectInkEntries[1].binding = 1;
    advectInkEntries[1].textureView = velocityTextureView;

    advectInkEntries[2] = {}; // ink texture (read)
    advectInkEntries[2].binding = 2;
    advectInkEntries[2].textureView = inkTextureView;

    advectInkEntries[3] = {}; // solid texture (read)
    advectInkEntries[3].binding = 3;
    advectInkEntries[3].textureView = solidTextureView;

    advectInkEntries[4] = {}; // new ink texture (write)
    advectInkEntries[4].binding = 4;
    advectInkEntries[4].textureView = newInkTextureView;

    WGPUBindGroupDescriptor advectInkDesc = {};
    advectInkDesc.layout = advectInkBindGroupLayout;
    advectInkDesc.entryCount = 5;
    advectInkDesc.entries = advectInkEntries;
    advectInkBindGroup = wgpuDeviceCreateBindGroup(device, &advectInkDesc);
    if (!advectInkBindGroup) {
        std::cerr << "Failed to create advect ink bind group" << std::endl;
        return false;
    }

    // boundary bind group
    WGPUBindGroupEntry boundaryEntries[6] = {};
    boundaryEntries[0] = {}; // uniform buffer
    boundaryEntries[0].binding = 0;
    boundaryEntries[0].buffer = uniformBuffer;
    boundaryEntries[0].offset = 0;
    boundaryEntries[0].size = sizeof(SimParams);

    boundaryEntries[1] = {}; // velocity texture (read)
    boundaryEntries[1].binding = 1;
    boundaryEntries[1].textureView = velocityTextureView;

    boundaryEntries[2] = {}; // solid texture (read)
    boundaryEntries[2].binding = 2;
    boundaryEntries[2].textureView = solidTextureView;

    boundaryEntries[3] = {}; // new velocity texture (write)
    boundaryEntries[3].binding = 3;
    boundaryEntries[3].textureView = newVelocityTextureView;

    boundaryEntries[4] = {}; // density texture (read)
    boundaryEntries[4].binding = 4;
    boundaryEntries[4].textureView = densityTextureView;

    boundaryEntries[5] = {}; // new density texture (write)
    boundaryEntries[5].binding = 5;
    boundaryEntries[5].textureView = newDensityTextureView;

    WGPUBindGroupDescriptor boundaryDesc = {};
    boundaryDesc.layout = boundaryBindGroupLayout;
    boundaryDesc.entryCount = 6;
    boundaryDesc.entries = boundaryEntries;
    boundaryBindGroup = wgpuDeviceCreateBindGroup(device, &boundaryDesc);
    if (!boundaryBindGroup) {
        std::cerr << "Failed to create boundary bind group" << std::endl;
        return false;
    }

    // boundary neighbors bind group (for clearing velocity components adjacent to solids)
    WGPUBindGroupEntry boundaryNeighborsEntries[4] = {};
    boundaryNeighborsEntries[0] = {}; // uniform buffer
    boundaryNeighborsEntries[0].binding = 0;
    boundaryNeighborsEntries[0].buffer = uniformBuffer;
    boundaryNeighborsEntries[0].offset = 0;
    boundaryNeighborsEntries[0].size = sizeof(SimParams);

    boundaryNeighborsEntries[1] = {}; // velocity texture (read)
    boundaryNeighborsEntries[1].binding = 1;
    boundaryNeighborsEntries[1].textureView = velocityTextureView;

    boundaryNeighborsEntries[2] = {}; // solid texture (read)
    boundaryNeighborsEntries[2].binding = 2;
    boundaryNeighborsEntries[2].textureView = solidTextureView;

    boundaryNeighborsEntries[3] = {}; // new velocity texture (write)
    boundaryNeighborsEntries[3].binding = 3;
    boundaryNeighborsEntries[3].textureView = newVelocityTextureView;

    WGPUBindGroupDescriptor boundaryNeighborsDesc = {};
    boundaryNeighborsDesc.layout = boundaryNeighborsBindGroupLayout;
    boundaryNeighborsDesc.entryCount = 4;
    boundaryNeighborsDesc.entries = boundaryNeighborsEntries;
    boundaryNeighborsBindGroup = wgpuDeviceCreateBindGroup(device, &boundaryNeighborsDesc);
    if (!boundaryNeighborsBindGroup) {
        std::cerr << "Failed to create boundary neighbors bind group" << std::endl;
        return false;
    }

    // vorticity compute bind group (for computing curl)
    WGPUBindGroupEntry vorticityComputeEntries[4] = {};
    vorticityComputeEntries[0] = {}; // uniform buffer
    vorticityComputeEntries[0].binding = 0;
    vorticityComputeEntries[0].buffer = uniformBuffer;
    vorticityComputeEntries[0].offset = 0;
    vorticityComputeEntries[0].size = sizeof(SimParams);

    vorticityComputeEntries[1] = {}; // velocity texture (read)
    vorticityComputeEntries[1].binding = 1;
    vorticityComputeEntries[1].textureView = velocityTextureView;

    vorticityComputeEntries[2] = {}; // solid texture (read)
    vorticityComputeEntries[2].binding = 2;
    vorticityComputeEntries[2].textureView = solidTextureView;

    vorticityComputeEntries[3] = {}; // curl texture (write)
    vorticityComputeEntries[3].binding = 3;
    vorticityComputeEntries[3].textureView = curlTextureView;

    WGPUBindGroupDescriptor vorticityComputeDesc = {};
    vorticityComputeDesc.layout = vorticityComputeBindGroupLayout;
    vorticityComputeDesc.entryCount = 4;
    vorticityComputeDesc.entries = vorticityComputeEntries;

    vorticityComputeBindGroup = wgpuDeviceCreateBindGroup(device, &vorticityComputeDesc);
    if (!vorticityComputeBindGroup) {
        std::cerr << "Failed to create vorticity compute bind group" << std::endl;
        return false;
    }

    // vorticity apply bind group (for applying forces)
    WGPUBindGroupEntry vorticityApplyEntries[5] = {};
    vorticityApplyEntries[0] = {}; // uniform buffer
    vorticityApplyEntries[0].binding = 0;
    vorticityApplyEntries[0].buffer = uniformBuffer;
    vorticityApplyEntries[0].offset = 0;
    vorticityApplyEntries[0].size = sizeof(SimParams);

    vorticityApplyEntries[1] = {}; // velocity texture (read)
    vorticityApplyEntries[1].binding = 1;
    vorticityApplyEntries[1].textureView = velocityTextureView;

    vorticityApplyEntries[2] = {}; // new velocity texture (write)
    vorticityApplyEntries[2].binding = 2;
    vorticityApplyEntries[2].textureView = newVelocityTextureView;

    vorticityApplyEntries[3] = {}; // solid texture (read)
    vorticityApplyEntries[3].binding = 3;
    vorticityApplyEntries[3].textureView = solidTextureView;

    vorticityApplyEntries[4] = {}; // curl texture (read)
    vorticityApplyEntries[4].binding = 4;
    vorticityApplyEntries[4].textureView = curlTextureView;

    WGPUBindGroupDescriptor vorticityApplyDesc = {};
    vorticityApplyDesc.layout = vorticityApplyBindGroupLayout;
    vorticityApplyDesc.entryCount = 5;
    vorticityApplyDesc.entries = vorticityApplyEntries;

    vorticityApplyBindGroup = wgpuDeviceCreateBindGroup(device, &vorticityApplyDesc);
    if (!vorticityApplyBindGroup) {
        std::cerr << "Failed to create vorticity apply bind group" << std::endl;
        return false;
    }

    // Create circle bind group
    WGPUBindGroupEntry circleEntries[5] = {};

    // uniform buffer
    circleEntries[0] = {};
    circleEntries[0].binding = 0;
    circleEntries[0].buffer = uniformBuffer;
    circleEntries[0].offset = 0;
    circleEntries[0].size = sizeof(SimParams);

    // solid texture (read-write)
    circleEntries[1] = {};
    circleEntries[1].binding = 1;
    circleEntries[1].textureView = solidTextureView;

    // velocity texture (read-only)
    circleEntries[2] = {};
    circleEntries[2].binding = 2;
    circleEntries[2].textureView = velocityTextureView;

    // new velocity texture (write-only)
    circleEntries[3] = {};
    circleEntries[3].binding = 3;
    circleEntries[3].textureView = newVelocityTextureView;

    // density texture (read-write)
    circleEntries[4] = {};
    circleEntries[4].binding = 4;
    circleEntries[4].textureView = densityTextureView;

    WGPUBindGroupDescriptor circleDesc = {};
    circleDesc.label = "Circle Bind Group";
    circleDesc.layout = circleBindGroupLayout;
    circleDesc.entryCount = 5;
    circleDesc.entries = circleEntries;

    circleBindGroup = wgpuDeviceCreateBindGroup(device, &circleDesc);
    if (!circleBindGroup) {
        std::cerr << "Failed to create circle bind group" << std::endl;
        return false;
    }

    return true;
}

void GPUFluidSimulator::copyInitialDataToGPU() {
    if (!webgpuInitialized) return;

    const auto& velX = cpuSimulator.getVelocityX();
    const auto& velY = cpuSimulator.getVelocityY();
    const auto& solid = cpuSimulator.getSolid();
    const auto& density = cpuSimulator.getDensity();

    
    // combined velocity data (RG32Float format)
    std::vector<float> velocityData(gridX * gridY * 2);
    for (int j = 0; j < gridY; j++) {
        for (int i = 0; i < gridX; i++) {
            int idx = j * gridX + i;
            velocityData[idx * 2] = velX[idx];
            velocityData[idx * 2 + 1] = velY[idx];
        }
    }

    // write velocity data to texture
    WGPUImageCopyTexture copy = {};
    copy.mipLevel = 0;
    copy.origin = {0, 0, 0};
    copy.aspect = WGPUTextureAspect_All;

    WGPUTextureDataLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = gridX * 8; // 2 floats * 4 bytes
    layout.rowsPerImage = gridY;

    WGPUExtent3D size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};

    // Copy to both main and new velocity textures
    copy.texture = velocityTexture;
    wgpuQueueWriteTexture(queue, &copy, velocityData.data(), velocityData.size() * sizeof(float), &layout, &size);
    copy.texture = newVelocityTexture;
    wgpuQueueWriteTexture(queue, &copy, velocityData.data(), velocityData.size() * sizeof(float), &layout, &size);

    // write density data to texture
    layout.bytesPerRow = gridX * 4; // 1 float * 4 bytes
    copy.texture = densityTexture;
    wgpuQueueWriteTexture(queue, &copy, density.data(), density.size() * sizeof(float), &layout, &size);
    copy.texture = newDensityTexture;
    wgpuQueueWriteTexture(queue, &copy, density.data(), density.size() * sizeof(float), &layout, &size);

    // write solid data to texture
    copy.texture = solidTexture;
    layout.bytesPerRow = gridX * 4; // 1 float * 4 bytes
    wgpuQueueWriteTexture(queue, &copy, solid.data(), solid.size() * sizeof(float), &layout, &size);

    // copy ink data if available
    if (cpuSimulator.isInkInitialized()) {
        const auto& redInk = cpuSimulator.getRedInk();
        const auto& greenInk = cpuSimulator.getGreenInk();
        const auto& blueInk = cpuSimulator.getBlueInk();

        // combined ink data (RGBA32Float format)
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

    updateUniformBuffer();
}

bool GPUFluidSimulator::createIntegratePipeline() {
    std::string shaderSource = ConfigLoader::readFile("compute_integrate.wgsl");
    if (shaderSource.empty()) {
        std::cerr << "Failed to read compute_integrate.wgsl" << std::endl;
        return false;
    }

    WGPUShaderModule computeShader = loadShader(shaderSource.c_str());
    if (!computeShader) {
        std::cerr << "Failed to create compute shader module" << std::endl;
        return false;
    }

    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = "Integrate Pipeline";
    pipelineDesc.compute.module = computeShader;
    pipelineDesc.compute.entryPoint = "integrateMain";
    pipelineDesc.layout = integratePipelineLayout;

    integratePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(computeShader);

    if (!integratePipeline) {
        std::cerr << "Failed to create integrate compute pipeline" << std::endl;
        return false;
    }

    return true;
}

bool GPUFluidSimulator::createProjectionPipelines() {
    if (!device) {
        std::cerr << "Error: WebGPU device is null when creating projection pipelines" << std::endl;
        return false;
    }

    // Create divergence pipeline
    {
        std::string divergenceShaderSource = ConfigLoader::readFile("compute_divergence.wgsl");
        if (divergenceShaderSource.empty()) {
            std::cerr << "Failed to read compute_divergence.wgsl" << std::endl;
            return false;
        }

        WGPUShaderModule divergenceShader = loadShader(divergenceShaderSource.c_str());
        if (!divergenceShader) {
            std::cerr << "Failed to create divergence shader module" << std::endl;
            return false;
        }

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.label = "Divergence Pipeline";
        pipelineDesc.layout = divergencePipelineLayout;
        pipelineDesc.compute.module = divergenceShader;
        pipelineDesc.compute.entryPoint = "computeDivergence";

        divergencePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
        wgpuShaderModuleRelease(divergenceShader);

        if (!divergencePipeline) {
            std::cerr << "Failed to create divergence pipeline" << std::endl;
            return false;
        }
    }

    // Create jacobi pressure pipeline
    {
        std::string jacobiShaderSource = ConfigLoader::readFile("compute_jacobi.wgsl");
        if (jacobiShaderSource.empty()) {
            std::cerr << "Failed to read compute_jacobi.wgsl" << std::endl;
            return false;
        }

        WGPUShaderModule jacobiShader = loadShader(jacobiShaderSource.c_str());
        if (!jacobiShader) {
            std::cerr << "Failed to create jacobi shader module" << std::endl;
            return false;
        }

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.label = "Jacobi Pipeline";
        pipelineDesc.layout = jacobiPipelineLayout;
        pipelineDesc.compute.module = jacobiShader;
        pipelineDesc.compute.entryPoint = "jacobiPressure";

        jacobiPressurePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
        wgpuShaderModuleRelease(jacobiShader);

        if (!jacobiPressurePipeline) {
            std::cerr << "Failed to create jacobi pressure pipeline" << std::endl;
            return false;
        }
    }

    // Create velocity update pipeline
    {
        std::string velocityUpdateShaderSource = ConfigLoader::readFile("compute_velocity_update.wgsl");
        if (velocityUpdateShaderSource.empty()) {
            std::cerr << "Failed to read compute_velocity_update.wgsl" << std::endl;
            return false;
        }

        WGPUShaderModule velocityUpdateShader = loadShader(velocityUpdateShaderSource.c_str());
        if (!velocityUpdateShader) {
            std::cerr << "Failed to create velocity update shader module" << std::endl;
            return false;
        }

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.label = "Velocity Update Pipeline";
        pipelineDesc.layout = velocityUpdatePipelineLayout;
        pipelineDesc.compute.module = velocityUpdateShader;
        pipelineDesc.compute.entryPoint = "updateVelocityFromPressure";

        velocityUpdatePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
        wgpuShaderModuleRelease(velocityUpdateShader);

        if (!velocityUpdatePipeline) {
            std::cerr << "Failed to create velocity update pipeline" << std::endl;
            return false;
        }
    }

    return true;
}

bool GPUFluidSimulator::createExtrapolatePipeline() {
    if (!device) {
        std::cerr << "Error: WebGPU device is null when creating extrapolate pipeline" << std::endl;
        return false;
    }

    std::string extrapolateShaderSource = ConfigLoader::readFile("compute_extrapolate.wgsl");
    if (extrapolateShaderSource.empty()) {
        std::cerr << "Failed to read compute_extrapolate.wgsl" << std::endl;
        return false;
    }

    WGPUShaderModule extrapolateShader = loadShader(extrapolateShaderSource.c_str());
    if (!extrapolateShader) {
        std::cerr << "Failed to create extrapolate shader module" << std::endl;
        return false;
    }

    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = "Extrapolate Pipeline";
    pipelineDesc.layout = extrapolatePipelineLayout;
    pipelineDesc.compute.module = extrapolateShader;
    pipelineDesc.compute.entryPoint = "extrapolate";

    extrapolatePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    if (!extrapolatePipeline) {
        std::cerr << "Failed to create extrapolate compute pipeline" << std::endl;
        wgpuShaderModuleRelease(extrapolateShader);
        return false;
    }

    wgpuShaderModuleRelease(extrapolateShader);
    return true;
}

bool GPUFluidSimulator::createAdvectPipelines() {
    if (!device) {
        std::cerr << "Error: WebGPU device is null when creating advect pipelines" << std::endl;
        return false;
    }

    // Create velocity advection pipeline
    {
        std::string shaderSource = ConfigLoader::readFile("compute_advect_velocity.wgsl");
        if (shaderSource.empty()) {
            std::cerr << "Failed to read compute_advect_velocity.wgsl" << std::endl;
            return false;
        }

        WGPUShaderModule shader = loadShader(shaderSource.c_str());
        if (!shader) {
            std::cerr << "Failed to create advect velocity shader module" << std::endl;
            return false;
        }

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.label = "Advect Velocity Pipeline";
        pipelineDesc.layout = advectVelocityPipelineLayout;
        pipelineDesc.compute.module = shader;
        pipelineDesc.compute.entryPoint = "advectVelocity";

        advectVelocityPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
        wgpuShaderModuleRelease(shader);

        if (!advectVelocityPipeline) {
            std::cerr << "Failed to create advect velocity pipeline" << std::endl;
            return false;
        }
    }

    // Create density advection pipeline
    {
        std::string shaderSource = ConfigLoader::readFile("compute_advect_density.wgsl");
        if (shaderSource.empty()) {
            std::cerr << "Failed to read compute_advect_density.wgsl" << std::endl;
            return false;
        }

        WGPUShaderModule shader = loadShader(shaderSource.c_str());
        if (!shader) {
            std::cerr << "Failed to create advect density shader module" << std::endl;
            return false;
        }

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.label = "Advect Density Pipeline";
        pipelineDesc.layout = advectDensityPipelineLayout;
        pipelineDesc.compute.module = shader;
        pipelineDesc.compute.entryPoint = "advectDensity";

        advectDensityPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
        wgpuShaderModuleRelease(shader);

        if (!advectDensityPipeline) {
            std::cerr << "Failed to create advect density pipeline" << std::endl;
            return false;
        }
    }

    // Create ink advection pipeline
    {
        std::string shaderSource = ConfigLoader::readFile("compute_advect_ink.wgsl");
        if (shaderSource.empty()) {
            std::cerr << "Failed to read compute_advect_ink.wgsl" << std::endl;
            return false;
        }

        WGPUShaderModule shader = loadShader(shaderSource.c_str());
        if (!shader) {
            std::cerr << "Failed to create advect ink shader module" << std::endl;
            return false;
        }

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.label = "Advect Ink Pipeline";
        pipelineDesc.layout = advectInkPipelineLayout;
        pipelineDesc.compute.module = shader;
        pipelineDesc.compute.entryPoint = "advectInk";

        advectInkPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
        wgpuShaderModuleRelease(shader);

        if (!advectInkPipeline) {
            std::cerr << "Failed to create advect ink pipeline" << std::endl;
            return false;
        }
    }

    return true;
}

bool GPUFluidSimulator::createBoundaryPipeline() {
    if (!device) {
        std::cerr << "Error: WebGPU device is null when creating boundary pipeline" << std::endl;
        return false;
    }

    std::string shaderSource = ConfigLoader::readFile("compute_boundary.wgsl");
    if (shaderSource.empty()) {
        std::cerr << "Failed to read compute_boundary.wgsl" << std::endl;
        return false;
    }

    WGPUShaderModule shader = loadShader(shaderSource.c_str());
    if (!shader) {
        std::cerr << "Failed to create boundary shader module" << std::endl;
        return false;
    }

    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = "Boundary Pipeline";
    pipelineDesc.layout = boundaryPipelineLayout;
    pipelineDesc.compute.module = shader;
    pipelineDesc.compute.entryPoint = "enforceBoundaryConditions";

    boundaryPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    wgpuShaderModuleRelease(shader);

    if (!boundaryPipeline) {
        std::cerr << "Failed to create boundary pipeline" << std::endl;
        return false;
    }

    return true;
}

bool GPUFluidSimulator::createBoundaryNeighborsPipeline() {
    if (!device) {
        std::cerr << "Error: WebGPU device is null when creating boundary neighbors pipeline" << std::endl;
        return false;
    }

    std::string shaderSource = ConfigLoader::readFile("compute_boundary_neighbors.wgsl");
    if (shaderSource.empty()) {
        std::cerr << "Failed to read compute_boundary_neighbors.wgsl" << std::endl;
        return false;
    }

    WGPUShaderModule shader = loadShader(shaderSource.c_str());
    if (!shader) {
        std::cerr << "Failed to create boundary neighbors shader module" << std::endl;
        return false;
    }

    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = "Boundary Neighbors Pipeline";
    pipelineDesc.layout = boundaryNeighborsPipelineLayout;
    pipelineDesc.compute.module = shader;
    pipelineDesc.compute.entryPoint = "clearNeighborVelocities";

    boundaryNeighborsPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    wgpuShaderModuleRelease(shader);

    if (!boundaryNeighborsPipeline) {
        std::cerr << "Failed to create boundary neighbors pipeline" << std::endl;
        return false;
    }

    return true;
}

bool GPUFluidSimulator::createVorticityPipelines() {
    if (!device) {
        std::cerr << "Error: WebGPU device is null when creating vorticity pipelines" << std::endl;
        return false;
    }

    // Create vorticity compute pipeline (for computing curl)
    {
        std::string shaderSource = ConfigLoader::readFile("compute_vorticity.wgsl");
        if (shaderSource.empty()) {
            std::cerr << "Failed to read compute_vorticity.wgsl" << std::endl;
            return false;
        }

        WGPUShaderModule shader = loadShader(shaderSource.c_str());
        if (!shader) {
            std::cerr << "Failed to create vorticity compute shader module" << std::endl;
            return false;
        }

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.label = "Vorticity Compute Pipeline";
        pipelineDesc.layout = vorticityComputePipelineLayout;
        pipelineDesc.compute.module = shader;
        pipelineDesc.compute.entryPoint = "computeCurl";

        vorticityComputePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
        wgpuShaderModuleRelease(shader);

        if (!vorticityComputePipeline) {
            std::cerr << "Failed to create vorticity compute pipeline" << std::endl;
            return false;
        }
    }

    // Create vorticity apply pipeline (for applying forces)
    {
        std::string shaderSource = ConfigLoader::readFile("compute_vorticity_apply.wgsl");
        if (shaderSource.empty()) {
            std::cerr << "Failed to read compute_vorticity_apply.wgsl" << std::endl;
            return false;
        }

        WGPUShaderModule shader = loadShader(shaderSource.c_str());
        if (!shader) {
            std::cerr << "Failed to create vorticity apply shader module" << std::endl;
            return false;
        }

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.label = "Vorticity Apply Pipeline";
        pipelineDesc.layout = vorticityApplyPipelineLayout;
        pipelineDesc.compute.module = shader;
        pipelineDesc.compute.entryPoint = "applyVorticityConfinement";

        vorticityApplyPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
        wgpuShaderModuleRelease(shader);

        if (!vorticityApplyPipeline) {
            std::cerr << "Failed to create vorticity apply pipeline" << std::endl;
            return false;
        }
    }

    return true;
}

bool GPUFluidSimulator::createCirclePipeline() {
    if (!device) {
        std::cerr << "Error: WebGPU device is null when creating circle pipeline" << std::endl;
        return false;
    }

    std::string shaderSource = ConfigLoader::readFile("compute_circle.wgsl");
    if (shaderSource.empty()) {
        std::cerr << "Failed to read compute_circle.wgsl" << std::endl;
        return false;
    }

    WGPUShaderModule shader = loadShader(shaderSource.c_str());
    if (!shader) {
        std::cerr << "Failed to create circle compute shader module" << std::endl;
        return false;
    }

    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = "Circle Pipeline";
    pipelineDesc.layout = circlePipelineLayout;
    pipelineDesc.compute.module = shader;
    pipelineDesc.compute.entryPoint = "updateCircle";

    circlePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    wgpuShaderModuleRelease(shader);

    if (!circlePipeline) {
        std::cerr << "Failed to create circle pipeline" << std::endl;
        return false;
    }

    return true;
}

void GPUFluidSimulator::dispatchCircle(WGPUCommandEncoder encoder) {
    WGPUComputePassDescriptor computePassDesc = {};
    computePassDesc.label = "Circle Compute Pass";

    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);
    wgpuComputePassEncoderSetLabel(computePass, "Circle Compute Pass");

    // Set pipeline and bind group
    wgpuComputePassEncoderSetPipeline(computePass, circlePipeline);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, circleBindGroup, 0, nullptr);

    // Dispatch workgroups for circle update (full grid for now)
    uint32_t workgroupX = (gridX + 15) / 16;
    uint32_t workgroupY = (gridY + 15) / 16;
    wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

    wgpuComputePassEncoderEnd(computePass);
    wgpuComputePassEncoderRelease(computePass);

    // Copy velocity back to main texture (double buffering)
    WGPUImageCopyTexture src = {};
    src.texture = newVelocityTexture;
    src.mipLevel = 0;
    src.origin = {0, 0, 0};
    src.aspect = WGPUTextureAspect_All;

    WGPUImageCopyTexture dst = {};
    dst.texture = velocityTexture;
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    WGPUExtent3D copySize = {
        static_cast<uint32_t>(gridX),
        static_cast<uint32_t>(gridY),
        1
    };

    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);
}

void GPUFluidSimulator::dispatchBoundaryConditions(WGPUCommandEncoder encoder) {
    WGPUComputePassDescriptor computePassDesc = {};
    computePassDesc.label = "Boundary Conditions Pass";
    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

    wgpuComputePassEncoderSetPipeline(computePass, boundaryPipeline);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, boundaryBindGroup, 0, nullptr);

    uint32_t workgroupX = (gridX + 15) / 16;
    uint32_t workgroupY = (gridY + 15) / 16;
    wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

    wgpuComputePassEncoderEnd(computePass);
    wgpuComputePassEncoderRelease(computePass);

    // Copy results back to main textures
    WGPUImageCopyTexture src = {};
    WGPUImageCopyTexture dst = {};
    WGPUExtent3D copySize = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};

    // Copy velocity
    src.texture = newVelocityTexture;
    src.mipLevel = 0;
    src.origin = {0, 0, 0};
    src.aspect = WGPUTextureAspect_All;

    dst.texture = velocityTexture;
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);

    // Copy density
    src.texture = newDensityTexture;
    dst.texture = densityTexture;
    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);
}

void GPUFluidSimulator::dispatchBoundaryNeighbors(WGPUCommandEncoder encoder) {
    WGPUComputePassDescriptor computePassDesc = {};
    computePassDesc.label = "Boundary Neighbors Pass";
    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

    wgpuComputePassEncoderSetPipeline(computePass, boundaryNeighborsPipeline);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, boundaryNeighborsBindGroup, 0, nullptr);

    uint32_t workgroupX = (gridX + 15) / 16;
    uint32_t workgroupY = (gridY + 15) / 16;
    wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

    wgpuComputePassEncoderEnd(computePass);
    wgpuComputePassEncoderRelease(computePass);

    // Copy velocity back to main texture (double buffering)
    WGPUImageCopyTexture src = {};
    src.texture = newVelocityTexture;
    src.mipLevel = 0;
    src.origin = {0, 0, 0};
    src.aspect = WGPUTextureAspect_All;

    WGPUImageCopyTexture dst = {};
    dst.texture = velocityTexture;
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    WGPUExtent3D copySize = {
        static_cast<uint32_t>(gridX),
        static_cast<uint32_t>(gridY),
        1
    };

    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);
}

void GPUFluidSimulator::updateUniformBuffer() {
    SimParams params = {};
    params.gridX = gridX;
    params.gridY = gridY;
    params.cellSize = cellSize;
    params.timeStep = config->simulation.timestep;
    params.gravity = config->simulation.gravity;
    params.vorticity = config->simulation.vorticity.strength;
    params.vorticityLen = config->simulation.vorticity.lengthScale;
    params.projectionIters = config->simulation.projection.iterations;
    params.overrelaxationCoeff = config->simulation.projection.overrelaxationCoefficient;
    params.density = config->simulation.fluidDensity;

    // Wind tunnel parameters - convert normalized positions to grid cells
    params.windTunnelSide = config->simulation.windTunnel.side;
    if (config->simulation.windTunnel.side == 0 || config->simulation.windTunnel.side == 3) {
        // Left or right - use Y dimension
        params.windTunnelStart = static_cast<int>(config->simulation.windTunnel.startPosition * gridY);
        params.windTunnelEnd = static_cast<int>(config->simulation.windTunnel.endPosition * gridY);
    } else {
        // Top or bottom - use X dimension
        params.windTunnelStart = static_cast<int>(config->simulation.windTunnel.startPosition * gridX);
        params.windTunnelEnd = static_cast<int>(config->simulation.windTunnel.endPosition * gridX);
    }
    params.windTunnelVelocity = config->simulation.windTunnel.velocity;

    // Circle parameters
    params.circleX = circleX;
    params.circleY = circleY;
    params.prevCircleX = prevCircleX;
    params.prevCircleY = prevCircleY;
    params.circleRadius = circleRadius;
    params.circleVelX = circleVelX;
    params.circleVelY = circleVelY;
    params.momentumTransferCoeff = momentumTransferCoeff;
    params.momentumTransferRadius = momentumTransferRadius;
    params.circleWasMoved = circleWasMoved;

    wgpuQueueWriteBuffer(queue, uniformBuffer, 0, &params, sizeof(SimParams));
}

void GPUFluidSimulator::dispatchIntegrate(WGPUCommandEncoder encoder) {
    // COMPUTE PASS
    WGPUComputePassDescriptor computePassDesc = {};
    computePassDesc.label = nullptr;
    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

    wgpuComputePassEncoderSetPipeline(computePass, integratePipeline);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, integrateBindGroup, 0, nullptr);

    // 16x16 workgroups
    uint32_t workgroupX = (gridX + 15) / 16;
    uint32_t workgroupY = (gridY + 15) / 16;

    wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);
    wgpuComputePassEncoderEnd(computePass);

    wgpuComputePassEncoderRelease(computePass);

    // copy backs
    WGPUImageCopyTexture src = {};
    src.texture = newVelocityTexture;
    src.mipLevel = 0;
    src.origin = {0, 0, 0};
    src.aspect = WGPUTextureAspect_All;

    WGPUImageCopyTexture dst = {};
    dst.texture = velocityTexture;
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    WGPUExtent3D copySize = {};
    copySize.width = gridX;
    copySize.height = gridY;
    copySize.depthOrArrayLayers = 1;

    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);
}

void GPUFluidSimulator::dispatchProjection(WGPUCommandEncoder encoder) {
    // Clear pressure textures each frame (CPU resets p to 0 every projection)
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
    {
        WGPUComputePassDescriptor computePassDesc = {};
        computePassDesc.label = "Divergence Pass";
        WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

        wgpuComputePassEncoderSetPipeline(computePass, divergencePipeline);
        wgpuComputePassEncoderSetBindGroup(computePass, 0, divergenceBindGroup, 0, nullptr);

        uint32_t workgroupX = (gridX + 15) / 16;
        uint32_t workgroupY = (gridY + 15) / 16;
        wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

        wgpuComputePassEncoderEnd(computePass);
        wgpuComputePassEncoderRelease(computePass);
    }

    // run jacobi pressure solver with ping-pong
    int iterations = static_cast<int>(config->simulation.projection.iterations);
    for (int iter = 0; iter < iterations; iter++) {
        bool writeToPressure = (iter % 2 == 1);

        WGPUComputePassDescriptor computePassDesc = {};
        computePassDesc.label = "Jacobi Pass";
        WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

        wgpuComputePassEncoderSetPipeline(computePass, jacobiPressurePipeline);
        wgpuComputePassEncoderSetBindGroup(
            computePass,
            0,
            writeToPressure ? jacobiPingPongBindGroup : jacobiBindGroup,
            0,
            nullptr);

        uint32_t workgroupX = (gridX + 15) / 16;
        uint32_t workgroupY = (gridY + 15) / 16;
        wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

        wgpuComputePassEncoderEnd(computePass);
        wgpuComputePassEncoderRelease(computePass);
    }

    // Ensure pressureTexture holds the latest result if iterations ended with write to newPressure
    if (iterations % 2 == 1) {
        WGPUImageCopyTexture src = {};
        src.texture = newPressureTexture;
        src.mipLevel = 0;
        src.origin = {0, 0, 0};
        src.aspect = WGPUTextureAspect_All;

        WGPUImageCopyTexture dst = {};
        dst.texture = pressureTexture;
        dst.mipLevel = 0;
        dst.origin = {0, 0, 0};
        dst.aspect = WGPUTextureAspect_All;

        WGPUExtent3D copySize = {};
        copySize.width = gridX;
        copySize.height = gridY;
        copySize.depthOrArrayLayers = 1;

        wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);
    }

    // update velocity after running pressure solver
    {
        WGPUComputePassDescriptor computePassDesc = {};
        computePassDesc.label = "Velocity Update Pass";
        WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

        wgpuComputePassEncoderSetPipeline(computePass, velocityUpdatePipeline);
        wgpuComputePassEncoderSetBindGroup(computePass, 0, velocityUpdateBindGroup, 0, nullptr);

        uint32_t workgroupX = (gridX + 15) / 16;
        uint32_t workgroupY = (gridY + 15) / 16;
        wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

        wgpuComputePassEncoderEnd(computePass);
        wgpuComputePassEncoderRelease(computePass);
    }

    // copy new velocity back to main velocity texture
    WGPUImageCopyTexture src = {};
    src.texture = newVelocityTexture;
    src.mipLevel = 0;
    src.origin = {0, 0, 0};
    src.aspect = WGPUTextureAspect_All;

    WGPUImageCopyTexture dst = {};
    dst.texture = velocityTexture;
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    WGPUExtent3D copySize = {};
    copySize.width = gridX;
    copySize.height = gridY;
    copySize.depthOrArrayLayers = 1;

    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);
}

void GPUFluidSimulator::dispatchExtrapolate(WGPUCommandEncoder encoder) {
    WGPUComputePassDescriptor computePassDesc = {};
    computePassDesc.label = "Extrapolate Pass";
    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

    wgpuComputePassEncoderSetPipeline(computePass, extrapolatePipeline);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, extrapolateBindGroup, 0, nullptr);

    uint32_t workgroupX = (gridX + 15) / 16;
    uint32_t workgroupY = (gridY + 15) / 16;
    wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

    wgpuComputePassEncoderEnd(computePass);
    wgpuComputePassEncoderRelease(computePass);

    // Copy new velocity back to main velocity texture
    WGPUImageCopyTexture src = {};
    src.texture = newVelocityTexture;
    src.mipLevel = 0;
    src.origin = {0, 0, 0};
    src.aspect = WGPUTextureAspect_All;

    WGPUImageCopyTexture dst = {};
    dst.texture = velocityTexture;
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    WGPUExtent3D copySize = {};
    copySize.width = gridX;
    copySize.height = gridY;
    copySize.depthOrArrayLayers = 1;

    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);
}

void GPUFluidSimulator::dispatchVorticity(WGPUCommandEncoder encoder) {
    // First pass: Compute curl
    {
        WGPUComputePassDescriptor computePassDesc = {};
        computePassDesc.label = "Vorticity Compute Curl Pass";
        WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

        wgpuComputePassEncoderSetPipeline(computePass, vorticityComputePipeline);
        wgpuComputePassEncoderSetBindGroup(computePass, 0, vorticityComputeBindGroup, 0, nullptr);

        uint32_t workgroupX = (gridX + 15) / 16;
        uint32_t workgroupY = (gridY + 15) / 16;
        wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

        wgpuComputePassEncoderEnd(computePass);
        wgpuComputePassEncoderRelease(computePass);
    }

    // Second pass: Apply vorticity confinement
    {
        WGPUComputePassDescriptor computePassDesc = {};
        computePassDesc.label = "Vorticity Apply Forces Pass";
        WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

        wgpuComputePassEncoderSetPipeline(computePass, vorticityApplyPipeline);
        wgpuComputePassEncoderSetBindGroup(computePass, 0, vorticityApplyBindGroup, 0, nullptr);

        uint32_t workgroupX = (gridX + 15) / 16;
        uint32_t workgroupY = (gridY + 15) / 16;
        wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

        wgpuComputePassEncoderEnd(computePass);
        wgpuComputePassEncoderRelease(computePass);
    }

    // Copy new velocity back to main velocity texture
    WGPUImageCopyTexture src = {};
    src.texture = newVelocityTexture;
    src.mipLevel = 0;
    src.origin = {0, 0, 0};
    src.aspect = WGPUTextureAspect_All;

    WGPUImageCopyTexture dst = {};
    dst.texture = velocityTexture;
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    WGPUExtent3D copySize = {};
    copySize.width = gridX;
    copySize.height = gridY;
    copySize.depthOrArrayLayers = 1;

    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);
}

void GPUFluidSimulator::dispatchAdvect(WGPUCommandEncoder encoder) {
    // Advect velocity
    {
        WGPUComputePassDescriptor computePassDesc = {};
        computePassDesc.label = "Advect Velocity Pass";
        WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

        wgpuComputePassEncoderSetPipeline(computePass, advectVelocityPipeline);
        wgpuComputePassEncoderSetBindGroup(computePass, 0, advectVelocityBindGroup, 0, nullptr);

        uint32_t workgroupX = (gridX + 15) / 16;
        uint32_t workgroupY = (gridY + 15) / 16;
        wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

        wgpuComputePassEncoderEnd(computePass);
        wgpuComputePassEncoderRelease(computePass);
    }

    // Advect density
    {
        WGPUComputePassDescriptor computePassDesc = {};
        computePassDesc.label = "Advect Density Pass";
        WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

        wgpuComputePassEncoderSetPipeline(computePass, advectDensityPipeline);
        wgpuComputePassEncoderSetBindGroup(computePass, 0, advectDensityBindGroup, 0, nullptr);

        uint32_t workgroupX = (gridX + 15) / 16;
        uint32_t workgroupY = (gridY + 15) / 16;
        wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

        wgpuComputePassEncoderEnd(computePass);
        wgpuComputePassEncoderRelease(computePass);
    }

    // Advect ink
    {
        WGPUComputePassDescriptor computePassDesc = {};
        computePassDesc.label = "Advect Ink Pass";
        WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);

        wgpuComputePassEncoderSetPipeline(computePass, advectInkPipeline);
        wgpuComputePassEncoderSetBindGroup(computePass, 0, advectInkBindGroup, 0, nullptr);

        uint32_t workgroupX = (gridX + 15) / 16;
        uint32_t workgroupY = (gridY + 15) / 16;
        wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupX, workgroupY, 1);

        wgpuComputePassEncoderEnd(computePass);
        wgpuComputePassEncoderRelease(computePass);
    }

    // Copy new textures back to main textures
    WGPUImageCopyTexture src = {};
    WGPUImageCopyTexture dst = {};
    WGPUExtent3D copySize = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};

    // Copy velocity
    src.texture = newVelocityTexture;
    src.mipLevel = 0;
    src.origin = {0, 0, 0};
    src.aspect = WGPUTextureAspect_All;

    dst.texture = velocityTexture;
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);

    // Copy density
    src.texture = newDensityTexture;
    dst.texture = densityTexture;
    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);

    // Copy ink
    src.texture = newInkTexture;
    dst.texture = inkTexture;
    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copySize);
}

WGPUShaderModule GPUFluidSimulator::loadShader(const char* source) {
    WGPUShaderModuleWGSLDescriptor shaderCodeDesc = {};
    shaderCodeDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    shaderCodeDesc.code = source;

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &shaderCodeDesc.chain;
    shaderDesc.label = "Compute Shader";

    return wgpuDeviceCreateShaderModule(device, &shaderDesc);
}
