#include "boilerplate.h"
#include "gpu_render.h"
#include "gpu_sim.h"
#include <sdl2webgpu.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>

// CONSTRUCTOR :250

// UPDATE RENDER PASS UNIFORM
void GPURenderer::updateUniformBufferRender(const ISimulator& simulator) {
    uniformData.gridX = simulator.gridX;
    uniformData.gridY = simulator.gridY;
    uniformData.cellSize = simulator.cellSize;
    uniformData.simWidth = uniformData.gridX * uniformData.cellSize;
    uniformData.simHeight = uniformData.gridY * uniformData.cellSize;

    // pressure range
    if (usingGPUTextures) {
        if (densityHistogramMin >= densityHistogramMax) {
            // initialize with reasonable values to prevent glitch from snowballing
            uniformData.pressureMin = -1.0f;
            uniformData.pressureMax = 1.0f;
        } else {
            // grab from GPU (already computed)
            uniformData.pressureMin = densityHistogramMin;
            uniformData.pressureMax = densityHistogramMax;
        }
    } else {
        // calculate from CPU pressure data
        const auto& pressure = simulator.getPressure();
        if (!pressure.empty()) {
            uniformData.pressureMin = *std::min_element(pressure.begin(), pressure.end());
            uniformData.pressureMax = *std::max_element(pressure.begin(), pressure.end());
            std::cout << "CPU pressure min/max: min=" << uniformData.pressureMin
                      << ", max=" << uniformData.pressureMax << std::endl;
        }
    }

    uniformData.densityHistogramMin = densityHistogramMin;
    uniformData.densityHistogramMax = densityHistogramMax;
    uniformData.velocityHistogramMin = velocityHistogramMin;
    uniformData.velocityHistogramMax = velocityHistogramMax;
    uniformData.densityHistogramMaxCount = densityHistogramMaxCount;
    uniformData.velocityHistogramMaxCount = velocityHistogramMaxCount;
    for (int i = 0; i < 16; i++) {
        uniformData.densityHistogramBins[i].x = densityHistogramBins[i * 4 + 0];
        uniformData.densityHistogramBins[i].y = densityHistogramBins[i * 4 + 1];
        uniformData.densityHistogramBins[i].z = densityHistogramBins[i * 4 + 2];
        uniformData.densityHistogramBins[i].w = densityHistogramBins[i * 4 + 3];
        uniformData.velocityHistogramBins[i].x = velocityHistogramBins[i * 4 + 0];
        uniformData.velocityHistogramBins[i].y = velocityHistogramBins[i * 4 + 1];
        uniformData.velocityHistogramBins[i].z = velocityHistogramBins[i * 4 + 2];
        uniformData.velocityHistogramBins[i].w = velocityHistogramBins[i * 4 + 3];
    }

    // update uniform buffer
    wgpuQueueWriteBuffer(queue, uniformBuffer, 0, &uniformData, sizeof(UniformData));
}

// BOILERPLATE INSTANTIATION HELPERS
bool GPURenderer::createTexture(const TextureDesc& desc, WGPUTexture& texture, WGPUTextureView& view) {
    texture = createTextureView(uniformData.gridX > 0 ? uniformData.gridX : 1,
                                uniformData.gridY > 0 ? uniformData.gridY : 1,
                                desc.format, desc.usage, view);
    return texture != nullptr;
}

void GPURenderer::copyTextureHostToDevice(WGPUTexture texture, const float* data, size_t dataSize, int gridX, int gridY, int channelCount) {
    WGPUImageCopyTexture copyTexture = {
        .texture = texture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
        .aspect = WGPUTextureAspect_All
    };

    WGPUTextureDataLayout dataLayout = {
        .offset = 0,
        .bytesPerRow = static_cast<uint32_t>(gridX * channelCount * sizeof(float)),
        .rowsPerImage = static_cast<uint32_t>(gridY)
    };

    WGPUExtent3D extent = {
        .width = static_cast<uint32_t>(gridX),
        .height = static_cast<uint32_t>(gridY),
        .depthOrArrayLayers = 1
    };

    wgpuQueueWriteTexture(queue, &copyTexture, data, dataSize * sizeof(float), &dataLayout, &extent);
}

WGPUSurfaceConfiguration GPURenderer::createSurfaceConfiguration() {
    WGPUSurfaceConfiguration config = {};
    config.nextInChain = nullptr;
    config.device = device;
    config.format = surfaceFormat;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = windowWidth;
    config.height = windowHeight;
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Opaque;
    return config;
}

// RENDER PIPELINE CREATION HELPERS
WGPUBindGroupLayout GPURenderer::createRenderBindGroupLayout(int textureCount, WGPUShaderStage visibility, size_t uniformSize) {
    std::vector<WGPUBindGroupLayoutEntry> entries;
    
    // uniform buffer
    auto uniformEntry = createUniformBufferLayoutEntry(0, uniformSize);
    uniformEntry.visibility = visibility;
    entries.push_back(uniformEntry);
    
    // sampler
    entries.push_back(createSamplerLayoutEntry(1, visibility));
    
    // textures
    for (int i = 2; i < 2 + textureCount; i++) {
        entries.push_back(createSampleTextureLayoutEntry(i, visibility));
    }
    
    return createBindGroupLayout(entries.size(), entries.data());
}

WGPUBindGroupLayoutEntry GPURenderer::createSamplerLayoutEntry(int binding, WGPUShaderStage visibility) {
    WGPUBindGroupLayoutEntry entry = {};
    entry.binding = binding;
    entry.visibility = visibility;
    entry.sampler.type = WGPUSamplerBindingType_NonFiltering;
    return entry;
}

WGPUBindGroupEntry GPURenderer::createSamplerBindGroupEntry(int binding, WGPUSampler sampler) {
    WGPUBindGroupEntry entry = {};
    entry.binding = binding;
    entry.sampler = sampler;
    return entry;
}

WGPURenderPassColorAttachment GPURenderer::createRenderPassColorAttachment(WGPUTextureView view, WGPULoadOp loadOp, WGPUStoreOp storeOp, WGPUColor clearValue) {
    WGPURenderPassColorAttachment attachment = {};
    attachment.view = view;
    attachment.resolveTarget = nullptr;
    attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    attachment.loadOp = loadOp;
    attachment.storeOp = storeOp;
    attachment.clearValue = clearValue;
    return attachment;
}

WGPURenderPassDescriptor GPURenderer::createRenderPassDescriptor(WGPURenderPassColorAttachment* colorAttachment) {
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.nextInChain = nullptr;
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = colorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    return renderPassDesc;
}

WGPUTextureViewDescriptor GPURenderer::createTextureViewDescriptor(WGPUTextureFormat format) {
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.nextInChain = nullptr;
    viewDesc.format = format;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    return viewDesc;
}

GPURenderer::RenderPipelineResult GPURenderer::createRenderPipelineWithLayout(
    const char* vertexShaderFile,
    const char* fragmentShaderFile,
    const char* fragmentEntry,
    WGPUTextureFormat surfaceFormat,
    int textureCount,
    WGPUShaderStage visibility,
    size_t uniformSize) {
    
    RenderPipelineResult result;

    // load shaders
    WGPUShaderModule vertexShader = createShaderModule(vertexShaderFile);
    if (!vertexShader) return result;
    
    WGPUShaderModule fragmentShader = createShaderModule(fragmentShaderFile);
    if (!fragmentShader) {
        wgpuShaderModuleRelease(vertexShader);
        return result;
    }

    result.bindGroupLayout = createRenderBindGroupLayout(textureCount, visibility, uniformSize);
    if (!result.bindGroupLayout) {
        wgpuShaderModuleRelease(vertexShader);
        wgpuShaderModuleRelease(fragmentShader);
        return result;
    }

    WGPUPipelineLayout pipelineLayout = createPipelineLayout(&result.bindGroupLayout, 1);
    if (!pipelineLayout) {
        wgpuBindGroupLayoutRelease(result.bindGroupLayout);
        wgpuShaderModuleRelease(vertexShader);
        wgpuShaderModuleRelease(fragmentShader);
        result.bindGroupLayout = nullptr;
        return result;
    }

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.nextInChain = nullptr;
    pipelineDesc.layout = pipelineLayout;
    
    // vertex state
    pipelineDesc.vertex.module = vertexShader;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.constantCount = 0;
    pipelineDesc.vertex.constants = nullptr;
    pipelineDesc.vertex.bufferCount = 0;
    pipelineDesc.vertex.buffers = nullptr;
    
    // primitive state
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    
    // multisample state
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;
    
    // fragment state
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = surfaceFormat;
    colorTarget.blend = nullptr;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    
    WGPUFragmentState fragmentState = {};
    fragmentState.module = fragmentShader;
    fragmentState.entryPoint = fragmentEntry;
    fragmentState.constantCount = 0;
    fragmentState.constants = nullptr;
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.depthStencil = nullptr;

    // create pipeline
    result.pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // clean up temporary objects
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(vertexShader);
    wgpuShaderModuleRelease(fragmentShader);

    if (!result.pipeline) {
        wgpuBindGroupLayoutRelease(result.bindGroupLayout);
        result.bindGroupLayout = nullptr;
    }

    return result;
}

GPURenderer::GPURenderer(SDL_Window* window, const Config& config)
    : window(window),
      drawTarget(config.rendering.target),
      showVelocityVectors(config.rendering.showVelocityVectors),
      disableHistograms(config.rendering.disableHistograms),
      velocityScale(config.rendering.velocityScale) {

    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    uniformData = {};
    uniformData.drawTarget = drawTarget;
    uniformData.drawVelocities = showVelocityVectors ? 1 : 0;
    uniformData.velScale = velocityScale;
    uniformData.windowWidth = static_cast<float>(windowWidth);
    uniformData.windowHeight = static_cast<float>(windowHeight);
    uniformData.disableHistograms = disableHistograms ? 1 : 0;
}

GPURenderer::~GPURenderer() {
    if (!initialized) return;

    if (device) wgpuDeviceTick(device);

    // textures
    RELEASE_TEXTURE_WITH_STORAGE(pressure)
    RELEASE_TEXTURE_WITH_STORAGE(solid)
    RELEASE_TEXTURE(density)
    RELEASE_TEXTURE(velocity)
    RELEASE_TEXTURE(blueInk)
    RELEASE_TEXTURE(greenInk)
    RELEASE_TEXTURE(redInk)

    // all the other stuff
    RELEASE_RENDER_PIPELINE(renderPipeline)
    RELEASE_RENDER_PIPELINE(renderPipelineGPU)
    RELEASE_BIND_GROUP(uniformBindGroup)
    RELEASE_BIND_GROUP(uniformBindGroupGPU)
    RELEASE_BIND_GROUP_LAYOUT(bindGroupLayout)
    RELEASE_BIND_GROUP_LAYOUT(bindGroupLayoutGPU)
    RELEASE_BUFFER(uniformBuffer)
    RELEASE_SAMPLER(sampler)

    // release device resources
    releaseResource(device, wgpuDeviceRelease);
    releaseResource(adapter, wgpuAdapterRelease);
    releaseResource(surface, wgpuSurfaceRelease);
    releaseResource(instance, wgpuInstanceRelease);
    initialized = false;
}

// MAIN RENDER LOOP
bool GPURenderer::init(const Config& config) {
    RETURN_FALSE_IF_FAIL(initDevice())
    
    surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    auto surfaceConfig = createSurfaceConfiguration();
    wgpuSurfaceConfigure(surface, &surfaceConfig);
    
    uniformBuffer = createBuffer(sizeof(UniformData), WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    RETURN_FALSE_IF_FAIL(uniformBuffer);

    sampler = createSampler(WGPUFilterMode_Nearest);
    RETURN_FALSE_IF_FAIL(sampler);

    RETURN_FALSE_IF_FAIL(initRenderPipeline())

    initialized = true;
    return true;
}

void GPURenderer::render(const ISimulator& simulator) {
    if (!initialized) return;
    usingGPUTextures = simulator.isUsingGPU();

    // compute histograms every n frames
    if (!disableHistograms && frameCount++ % HISTOGRAM_FRAME_INTERVAL == 0) {
        computeHistograms(simulator);
    }

    updateUniformBufferRender(simulator);
    updateTextures(simulator);

    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
    WGPUTextureView nextTexture = wgpuTextureCreateView(surfaceTexture.texture, nullptr);

    // command encoder
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

    // render pass
    auto colorAttachment = createRenderPassColorAttachment(
        nextTexture,
        WGPULoadOp_Clear,
        WGPUStoreOp_Store,
        {0.0f, 0.0f, 0.0f, 1.0f}
    );
    auto renderPassDesc = createRenderPassDescriptor(&colorAttachment);

    WGPURenderPassEncoder renderPassEncoder = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    if (!renderPassEncoder) {
        wgpuTextureViewRelease(nextTexture);
        wgpuCommandEncoderRelease(encoder);
        return;
    }

    // set pipeline and bind groups based on mode
    if (usingGPUTextures) {
        wgpuRenderPassEncoderSetPipeline(renderPassEncoder, renderPipelineGPU);
        wgpuRenderPassEncoderSetBindGroup(renderPassEncoder, 0, uniformBindGroupGPU, 0, nullptr);
    } else {
        wgpuRenderPassEncoderSetPipeline(renderPassEncoder, renderPipeline);
        wgpuRenderPassEncoderSetBindGroup(renderPassEncoder, 0, uniformBindGroup, 0, nullptr);
    }

    // draw fullscreen quad
    wgpuRenderPassEncoderDraw(renderPassEncoder, 6, 1, 0, 0);

    // END render pass
    wgpuRenderPassEncoderEnd(renderPassEncoder);
    wgpuRenderPassEncoderRelease(renderPassEncoder);

    // submit commands
    WGPUCommandBufferDescriptor cmdBufferDesc = WGPUCommandBufferDescriptor{};
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(queue, 1, &commands);

    // draw
    wgpuSurfacePresent(surface);

    // release surface texture
    if (surfaceTexture.texture) {
        wgpuTextureRelease(surfaceTexture.texture);
    }

    // clean up
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(nextTexture);
    wgpuInstanceProcessEvents(instance);
}

void GPURenderer::updateTextures(const ISimulator& simulator) {
    int gridX = simulator.gridX;
    int gridY = simulator.gridY;

    // GPU MODE
    if (usingGPUTextures) {
        const auto& gpuSim = static_cast<const GPUSimulator&>(simulator);

        if (!pressureTextureView || uniformData.gridX != gridX || uniformData.gridY != gridY) {
            RELEASE_TEXTURE_VIEW(pressure);
            releaseResource(pressureTextureStorageView, wgpuTextureViewRelease);
            RELEASE_TEXTURE_VIEW(density);
            RELEASE_TEXTURE_VIEW(velocity);
            RELEASE_TEXTURE_VIEW(solid);
            releaseResource(solidTextureStorageView, wgpuTextureViewRelease);
            RELEASE_TEXTURE_VIEW(redInk);

            auto viewDescR32 = createTextureViewDescriptor(WGPUTextureFormat_R32Float);
            pressureTextureView = wgpuTextureCreateView(gpuSim.getPressureTexture(), &viewDescR32);
            pressureTextureStorageView = wgpuTextureCreateView(gpuSim.getPressureTexture(), &viewDescR32);
            densityTextureView = wgpuTextureCreateView(gpuSim.getDensityTexture(), &viewDescR32);
            solidTextureView = wgpuTextureCreateView(gpuSim.getSolidTexture(), &viewDescR32);
            solidTextureStorageView = wgpuTextureCreateView(gpuSim.getSolidTexture(), &viewDescR32);

            auto viewDescRG32 = createTextureViewDescriptor(WGPUTextureFormat_RG32Float);
            velocityTextureView = wgpuTextureCreateView(gpuSim.getVelocityTexture(), &viewDescRG32);

            auto viewDescRGBA32 = createTextureViewDescriptor(WGPUTextureFormat_RGBA32Float);
            // reusing redInkTextureView for the combined ink texture
            redInkTextureView = wgpuTextureCreateView(gpuSim.getInkTexture(), &viewDescRGBA32);
        }

        // recreate gpu bind group when texture views are updated
        if (uniformBindGroupGPU) {
            wgpuBindGroupRelease(uniformBindGroupGPU);
            uniformBindGroupGPU = nullptr;
        }

        // GPU bind group (to support different ink texture format)
        std::vector<WGPUBindGroupEntry> bindGroupEntries;
        bindGroupEntries.push_back(createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(UniformData)));
        bindGroupEntries.push_back(createSamplerBindGroupEntry(1, sampler));
        bindGroupEntries.push_back(createTextureViewBindGroupEntry(2, pressureTextureView));
        bindGroupEntries.push_back(createTextureViewBindGroupEntry(3, densityTextureView));
        bindGroupEntries.push_back(createTextureViewBindGroupEntry(4, velocityTextureView));
        bindGroupEntries.push_back(createTextureViewBindGroupEntry(5, solidTextureView));
        bindGroupEntries.push_back(createTextureViewBindGroupEntry(6, redInkTextureView)); // misleading; RGBA ink texture

        uniformBindGroupGPU = createBindGroup(bindGroupEntries.size(), bindGroupEntries.data(), bindGroupLayoutGPU);
        uniformData.gridX = gridX;
        uniformData.gridY = gridY;

        return;
    }

    // CPU MODE
    // create textures initially or on resize
    if (!pressureTexture || uniformData.gridX != gridX || uniformData.gridY != gridY) {
        // release old textures (views first, then textures)
        RELEASE_TEXTURE_VIEW(pressure);
        releaseResource(pressureTextureStorageView, wgpuTextureViewRelease);
        RELEASE_TEXTURE_VIEW(density);
        RELEASE_TEXTURE_VIEW(velocity);
        RELEASE_TEXTURE_VIEW(solid);
        RELEASE_TEXTURE_VIEW(redInk);
        RELEASE_TEXTURE_VIEW(greenInk);
        RELEASE_TEXTURE_VIEW(blueInk);
        
        releaseResource(pressureTexture, wgpuTextureRelease);
        releaseResource(densityTexture, wgpuTextureRelease);
        releaseResource(velocityTexture, wgpuTextureRelease);
        releaseResource(solidTexture, wgpuTextureRelease);
        releaseResource(redInkTexture, wgpuTextureRelease);
        releaseResource(greenInkTexture, wgpuTextureRelease);
        releaseResource(blueInkTexture, wgpuTextureRelease);

        // release old bind group before creating new textures
        RELEASE_BIND_GROUP(uniformBindGroup);

        // create new textures (CPU mode - simple textures)
        pressureTexture = createTextureView(gridX, gridY, WGPUTextureFormat_R32Float, TEXTURE_BINDING_FLAGS, pressureTextureView);
        densityTexture = createTextureView(gridX, gridY, WGPUTextureFormat_R32Float, TEXTURE_BINDING_FLAGS, densityTextureView);
        velocityTexture = createTextureView(gridX, gridY, WGPUTextureFormat_RG32Float, TEXTURE_BINDING_FLAGS, velocityTextureView);
        solidTexture = createTextureView(gridX, gridY, WGPUTextureFormat_R32Float, TEXTURE_BINDING_FLAGS, solidTextureView);
        redInkTexture = createTextureView(gridX, gridY, WGPUTextureFormat_R32Float, TEXTURE_BINDING_FLAGS, redInkTextureView);
        greenInkTexture = createTextureView(gridX, gridY, WGPUTextureFormat_R32Float, TEXTURE_BINDING_FLAGS, greenInkTextureView);
        blueInkTexture = createTextureView(gridX, gridY, WGPUTextureFormat_R32Float, TEXTURE_BINDING_FLAGS, blueInkTextureView);

        // create bind groups
        std::vector<WGPUBindGroupEntry> bindGroupEntries;
        bindGroupEntries.push_back(createUniformBufferBindGroupEntry(0, uniformBuffer, sizeof(UniformData)));
        bindGroupEntries.push_back(createSamplerBindGroupEntry(1, sampler));
        bindGroupEntries.push_back(createTextureViewBindGroupEntry(2, pressureTextureView));
        bindGroupEntries.push_back(createTextureViewBindGroupEntry(3, densityTextureView));
        bindGroupEntries.push_back(createTextureViewBindGroupEntry(4, velocityTextureView));
        bindGroupEntries.push_back(createTextureViewBindGroupEntry(5, solidTextureView));
        bindGroupEntries.push_back(createTextureViewBindGroupEntry(6, redInkTextureView));
        bindGroupEntries.push_back(createTextureViewBindGroupEntry(7, greenInkTextureView));
        bindGroupEntries.push_back(createTextureViewBindGroupEntry(8, blueInkTextureView));

        uniformBindGroup = createBindGroup(bindGroupEntries.size(), bindGroupEntries.data(), bindGroupLayout);
    }

    // update texture data
    const auto& pressure = simulator.getPressure();
    const auto& density = simulator.getDensity();
    const auto& velocityX = simulator.getVelocityX();
    const auto& velocityY = simulator.getVelocityY();
    const auto& solid = simulator.getSolid();

    if (!pressure.empty()) {
        copyTextureHostToDevice(pressureTexture, pressure.data(), pressure.size(), gridX, gridY);

        if (!density.empty()) {
            copyTextureHostToDevice(densityTexture, density.data(), density.size(), gridX, gridY);
        }

        // write velocity data to texture (interleaved RG format)
        std::vector<float> velocityData;
        velocityData.reserve(pressure.size() * 2);
        for (size_t i = 0; i < pressure.size(); ++i) {
            velocityData.push_back(velocityX[i]);
            velocityData.push_back(velocityY[i]);
        }

        if (!velocityData.empty()) {
            copyTextureHostToDevice(velocityTexture, velocityData.data(), velocityData.size(), gridX, gridY, 2);
        }

        if (!solid.empty()) {
            copyTextureHostToDevice(solidTexture, solid.data(), solid.size(), gridX, gridY);
        }

        // write ink data to textures
        const auto& redInk = simulator.getRedInk();
        const auto& greenInk = simulator.getGreenInk();
        const auto& blueInk = simulator.getBlueInk();

        if (simulator.inkInitialized) {
            if (!redInk.empty()) {
                copyTextureHostToDevice(redInkTexture, redInk.data(), redInk.size(), gridX, gridY);
            }

            if (!greenInk.empty()) {
                copyTextureHostToDevice(greenInkTexture, greenInk.data(), greenInk.size(), gridX, gridY);
            }

            if (!blueInk.empty()) {
                copyTextureHostToDevice(blueInkTexture, blueInk.data(), blueInk.size(), gridX, gridY);
            }
        }
    }
}

// HISTOGRAM HELPER
void GPURenderer::computeHistograms(const ISimulator& simulator) {
    if (usingGPUTextures) {
        // grab data from GPU simulator
        const auto& gpuSim = static_cast<const GPUSimulator&>(simulator);

        int readySlot = -1;
        const float* pressureMinMax = nullptr;
        const float* velocityMinMax = nullptr;
        const int* histogramBins = nullptr;

        if (gpuSim.getHistogramData(readySlot, pressureMinMax, velocityMinMax, histogramBins)) {
            if (pressureMinMax[0] < pressureMinMax[1]) {
                densityHistogramMin = pressureMinMax[0];
                densityHistogramMax = pressureMinMax[1];
            }
            if (velocityMinMax[0] < velocityMinMax[1]) {
                velocityHistogramMin = velocityMinMax[0];
                velocityHistogramMax = velocityMinMax[1];
            }

            // copy bins from GPU
            for (int i = 0; i < 64; i++) {
                densityHistogramBins[i] = histogramBins[i];
                velocityHistogramBins[i] = histogramBins[64 + i];
            }

            // compute max counts
            densityHistogramMaxCount = 0;
            velocityHistogramMaxCount = 0;
            for (int i = 0; i < IRenderer::HISTOGRAM_BINS; i++) {
                densityHistogramMaxCount = std::max(densityHistogramMaxCount, densityHistogramBins[i]);
                velocityHistogramMaxCount = std::max(velocityHistogramMaxCount, velocityHistogramBins[i]);
            }

            gpuSim.advanceHistogramReadIndex();
        }
    } else {
        // CPU mode calculator implemented in irenderer since it is reused between CPU/HYBRID modes
        IRenderer::HistogramData data;
        data.densityHistogramBins = densityHistogramBins;
        data.velocityHistogramBins = velocityHistogramBins;

        IRenderer::computeHistograms(simulator, data);

        densityHistogramMin = data.densityHistogramMin;
        densityHistogramMax = data.densityHistogramMax;
        velocityHistogramMin = data.velocityHistogramMin;
        velocityHistogramMax = data.velocityHistogramMax;
        densityHistogramBins = data.densityHistogramBins;
        velocityHistogramBins = data.velocityHistogramBins;

        // compute max counts
        densityHistogramMaxCount = 0;
        velocityHistogramMaxCount = 0;
        for (int i = 0; i < IRenderer::HISTOGRAM_BINS; i++) {
            densityHistogramMaxCount = std::max(densityHistogramMaxCount, densityHistogramBins[i]);
            velocityHistogramMaxCount = std::max(velocityHistogramMaxCount, velocityHistogramBins[i]);
        }
    }
}

// GPU INITIALIZATION _BOILERPLATE_
bool GPURenderer::initDevice() {
    WGPUInstanceDescriptor instanceDesc = {};
    instanceDesc.nextInChain = nullptr;

    instance = wgpuCreateInstance(&instanceDesc);
    RETURN_FALSE_IF_FAIL(instance);

    // get surface from SDL window
    surface = SDL_GetWGPUSurface(instance, window);
    RETURN_FALSE_IF_FAIL(surface);

    struct UserData {
        WGPUAdapter adapter = nullptr;
        bool requestEnded = false;
    };

    UserData userData;

    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, char const* message, void* userdata) {
        UserData* userData = static_cast<UserData*>(userdata);
        if (status == WGPURequestAdapterStatus_Success) {
            userData->adapter = adapter;
        } else {
            std::cerr << "ERR getting WebGPU adapter: " << message << std::endl;
        }
        userData->requestEnded = true;
    };

    WGPURequestAdapterOptions adapterOptions = {};
    adapterOptions.nextInChain = nullptr;
    adapterOptions.compatibleSurface = surface;
    adapterOptions.powerPreference = WGPUPowerPreference_HighPerformance;

    wgpuInstanceRequestAdapter(instance, &adapterOptions, onAdapterRequestEnded, &userData);

    while (!userData.requestEnded) {
        // wait for adapter request to complete
        // this is bad :)
    }

    if (!userData.adapter) {
        std::cerr << "ERR getting WebGPU adapter" << std::endl;
        return false;
    }

    adapter = userData.adapter;

    struct DeviceData {
        WGPUDevice device = nullptr;
        bool requestEnded = false;
    };

    DeviceData deviceData;

    auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status, WGPUDevice device, char const* message, void* userdata) {
        DeviceData* deviceData = static_cast<DeviceData*>(userdata);
        if (status == WGPURequestDeviceStatus_Success) {
            deviceData->device = device;
        } else {
            std::cerr << "ERR getting WebGPU device: " << message << std::endl;
        }
        deviceData->requestEnded = true;
    };

    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.nextInChain = nullptr;

    wgpuAdapterRequestDevice(adapter, &deviceDesc, onDeviceRequestEnded, &deviceData);

    while (!deviceData.requestEnded) {
        // wait for device request to complete
        // still bad :(
    }

    if (!deviceData.device) {
        std::cerr << "ERR getting WebGPU device" << std::endl;
        return false;
    }

    device = deviceData.device;
    queue = wgpuDeviceGetQueue(device);

    // error callback required
    wgpuDeviceSetUncapturedErrorCallback(device,
        [](WGPUErrorType type, const char* message, void* userdata) {
            std::cerr << "WebGPU ERR: " << type << " - " << (message ? message : "[NO MESSAGE]") << std::endl;
        }, nullptr);

    return true;
}

bool GPURenderer::initRenderPipeline() {
    auto cpuResult = createRenderPipelineWithLayout(
        "vertex.wgsl",
        "fragment.wgsl",
        "fs_main",
        surfaceFormat,
        7,
        WGPUShaderStage_Fragment,
        sizeof(UniformData)
    );
    RETURN_FALSE_IF_FAIL(cpuResult.pipeline);

    renderPipeline = cpuResult.pipeline;
    bindGroupLayout = cpuResult.bindGroupLayout;

    auto gpuResult = createRenderPipelineWithLayout(
        "vertex.wgsl",
        "fragment_gpu.wgsl",
        "fs_main",
        surfaceFormat,
        5,
        WGPUShaderStage_Fragment,
        sizeof(UniformData)
    );
    RETURN_FALSE_IF_FAIL(gpuResult.pipeline);

    renderPipelineGPU = gpuResult.pipeline;
    bindGroupLayoutGPU = gpuResult.bindGroupLayout;

    return true;
}