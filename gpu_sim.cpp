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
    waterTexture(nullptr),
    newVelocityTexture(nullptr),
    newDensityTexture(nullptr),
    newInkTexture(nullptr),
    velocityTextureView(nullptr),
    pressureTextureView(nullptr),
    densityTextureView(nullptr),
    solidTextureView(nullptr),
    inkTextureView(nullptr),
    waterTextureView(nullptr),
    newVelocityTextureView(nullptr),
    newDensityTextureView(nullptr),
    newInkTextureView(nullptr),
    sampler(nullptr),
    uniformBuffer(nullptr),
    advectPipelineLayout(nullptr),
    projectPipelineLayout(nullptr),
    diffusionPipelineLayout(nullptr),
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
    gridX(0),
    gridY(0) {
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
    if (webgpuInitialized && device && queue) {
        if (!initGPUResources()) {
            std::cerr << "Error: Failed to initialize GPU resources after CPU init" << std::endl;
        }
    }
}

void GPUFluidSimulator::update() {
    dispatchIntegrate();
    
    // TODO port rest of simulation
}

void GPUFluidSimulator::onMouseDown(int gridX, int gridY) {
    cpuSimulator.onMouseDown(gridX, gridY);
}

void GPUFluidSimulator::onMouseDrag(int gridX, int gridY) {
    cpuSimulator.onMouseDrag(gridX, gridY);
}

void GPUFluidSimulator::onMouseUp() {
    cpuSimulator.onMouseUp();
}

// holy boilerplate
bool GPUFluidSimulator::initGPUResources() {
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

    copyInitialDataToGPU();

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
    if (projectPipelineLayout) {
        wgpuPipelineLayoutRelease(projectPipelineLayout);
        projectPipelineLayout = nullptr;
    }
    if (diffusionPipelineLayout) {
        wgpuPipelineLayoutRelease(diffusionPipelineLayout);
        diffusionPipelineLayout = nullptr;
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
    if (integratePipeline) {
        wgpuComputePipelineRelease(integratePipeline);
        integratePipeline = nullptr;
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
    if (waterTextureView) {
        wgpuTextureViewRelease(waterTextureView);
        waterTextureView = nullptr;
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
    if (waterTexture) {
        wgpuTextureRelease(waterTexture);
        waterTexture = nullptr;
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
    pressureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst;
    pressureDesc.dimension = WGPUTextureDimension_2D;
    pressureDesc.mipLevelCount = 1;
    pressureDesc.sampleCount = 1;
    pressureTexture = wgpuDeviceCreateTexture(device, &pressureDesc);
    if (!pressureTexture) {
        std::cerr << "Failed to create pressure texture" << std::endl;
        return false;
    }

    WGPUTextureDescriptor densityDesc = {};
    densityDesc.size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    densityDesc.format = WGPUTextureFormat_R32Float;
    densityDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst;
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
    solidDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst;
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
    inkDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst;
    inkDesc.dimension = WGPUTextureDimension_2D;
    inkDesc.mipLevelCount = 1;
    inkDesc.sampleCount = 1;
    inkTexture = wgpuDeviceCreateTexture(device, &inkDesc);
    if (!inkTexture) {
        std::cerr << "Failed to create ink texture" << std::endl;
        return false;
    }

    WGPUTextureDescriptor waterDesc = {};
    waterDesc.size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    waterDesc.format = WGPUTextureFormat_R32Float;
    waterDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst;
    waterDesc.dimension = WGPUTextureDimension_2D;
    waterDesc.mipLevelCount = 1;
    waterDesc.sampleCount = 1;
    waterTexture = wgpuDeviceCreateTexture(device, &waterDesc);
    if (!waterTexture) {
        std::cerr << "Failed to create water texture" << std::endl;
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

    velocityTextureView = wgpuTextureCreateView(velocityTexture, nullptr);
    pressureTextureView = wgpuTextureCreateView(pressureTexture, nullptr);
    densityTextureView = wgpuTextureCreateView(densityTexture, nullptr);
    solidTextureView = wgpuTextureCreateView(solidTexture, nullptr);
    inkTextureView = wgpuTextureCreateView(inkTexture, nullptr);
    waterTextureView = wgpuTextureCreateView(waterTexture, nullptr);
    newVelocityTextureView = wgpuTextureCreateView(newVelocityTexture, nullptr);
    newDensityTextureView = wgpuTextureCreateView(newDensityTexture, nullptr);
    newInkTextureView = wgpuTextureCreateView(newInkTexture, nullptr);

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
    int gridX, gridY;
    float cellSize;
    float timeStep;
    float gravity;
    float diffusionRate;
    float vorticity;
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
    WGPUBindGroupLayoutEntry entries[4] = {};

    // uniform buffer
    entries[0] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = 0;
    entries[0].buffer.hasDynamicOffset = false;

    // old velocity texture (read)
    entries[1] = {};
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    entries[1].storageTexture.format = WGPUTextureFormat_RG32Float;
    entries[1].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    // new velocity texture (write)
    entries[2] = {};
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[2].storageTexture.format = WGPUTextureFormat_RG32Float;
    entries[2].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    // solid texture (read)
    entries[3] = {};
    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Compute;
    entries[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[3].texture.multisampled = false;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 4;
    layoutDesc.entries = entries;

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

    // TODO future steps
    WGPUPipelineLayoutDescriptor emptyLayoutDesc = {};
    emptyLayoutDesc.bindGroupLayoutCount = 0;
    emptyLayoutDesc.bindGroupLayouts = nullptr;

    advectPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &emptyLayoutDesc);
    projectPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &emptyLayoutDesc);
    diffusionPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &emptyLayoutDesc);

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

    return true;
}

// TEMP ?
void GPUFluidSimulator::copyInitialDataToGPU() {
    if (!webgpuInitialized) return;

    const auto& velX = cpuSimulator.getVelocityX();
    const auto& velY = cpuSimulator.getVelocityY();
    const auto& solid = cpuSimulator.getSolid();

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
    copy.texture = velocityTexture;
    copy.mipLevel = 0;
    copy.origin = {0, 0, 0};
    copy.aspect = WGPUTextureAspect_All;

    WGPUTextureDataLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = gridX * 8; // 2 floats * 4 bytes
    layout.rowsPerImage = gridY;

    WGPUExtent3D size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
    wgpuQueueWriteTexture(queue, &copy, velocityData.data(), velocityData.size() * sizeof(float), &layout, &size);

    updateUniformBuffer();
}

bool GPUFluidSimulator::createIntegratePipeline() {
    std::string shaderSource = ConfigLoader::readFile("compute.wgsl");
    if (shaderSource.empty()) {
        std::cerr << "Failed to read compute.wgsl" << std::endl;
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

void GPUFluidSimulator::updateUniformBuffer() {
    SimParams params = {};
    params.gridX = gridX;
    params.gridY = gridY;
    params.cellSize = cpuSimulator.getCellSize();
    params.timeStep = config->simulation.timestep;
    params.gravity = config->simulation.gravity;
    params.diffusionRate = config->ink.diffusionRate;
    params.vorticity = config->simulation.vorticity.strength;

    wgpuQueueWriteBuffer(queue, uniformBuffer, 0, &params, sizeof(SimParams));
}

void GPUFluidSimulator::dispatchIntegrate() {
    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.label = nullptr;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

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

    // submit commands
    WGPUCommandBufferDescriptor cmdDesc = {};
    cmdDesc.label = nullptr;
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cmdDesc);

    wgpuQueueSubmit(queue, 1, &commands);

    // clean up
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuComputePassEncoderRelease(computePass);
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
