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
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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

    // Update viewport configuration from g_layoutPixels
    uniformData.viewportCount = 0;
    for (int i = 0; i < 4; i++) {
        std::string vpName = "viewport_" + std::to_string(i + 1);
        auto it = g_layoutPixels.components.find(vpName);
        if (it != g_layoutPixels.components.end()) {
            uniformData.viewportX[i] = it->second.x;
            uniformData.viewportY[i] = it->second.y;
            uniformData.viewportWidth[i] = it->second.width;
            uniformData.viewportHeight[i] = it->second.height;
            auto cfgIt = g_config.layout.components.find(vpName);
            uniformData.viewportRenderTarget[i] = (cfgIt != g_config.layout.components.end())
                ? cfgIt->second.target : 2;
            uniformData.viewportRenderVelocity[i] = (cfgIt != g_config.layout.components.end())
                ? (cfgIt->second.velocity ? 1 : 0) : 0;
            uniformData.viewportCount++;
        }
    }

    // Update histogram configuration from g_layoutPixels
    auto dhIt = g_layoutPixels.components.find("density_histogram");
    auto dhCfgIt = g_config.layout.components.find("density_histogram");
    if (dhIt != g_layoutPixels.components.end() && dhCfgIt != g_config.layout.components.end()) {
        uniformData.densityHistogramEnabled = dhCfgIt->second.enabled ? 1 : 0;
        uniformData.densityHistogramX = dhIt->second.x;
        uniformData.densityHistogramY = dhIt->second.y;
        uniformData.densityHistogramWidth = dhIt->second.width;
        uniformData.densityHistogramHeight = dhIt->second.height;
    }

    auto vhIt = g_layoutPixels.components.find("velocity_histogram");
    auto vhCfgIt = g_config.layout.components.find("velocity_histogram");
    if (vhIt != g_layoutPixels.components.end() && vhCfgIt != g_config.layout.components.end()) {
        uniformData.velocityHistogramEnabled = vhCfgIt->second.enabled ? 1 : 0;
        uniformData.velocityHistogramX = vhIt->second.x;
        uniformData.velocityHistogramY = vhIt->second.y;
        uniformData.velocityHistogramWidth = vhIt->second.width;
        uniformData.velocityHistogramHeight = vhIt->second.height;
    }

    auto ethIt = g_layoutPixels.components.find("entropy_time_series");
    if (ethIt == g_layoutPixels.components.end()) {
        ethIt = g_layoutPixels.components.find("entropy_histogram");
    }

    auto ethCfgIt = g_config.layout.components.find("entropy_time_series");
    if (ethCfgIt == g_config.layout.components.end()) {
        ethCfgIt = g_config.layout.components.find("entropy_histogram");
    }

    if (ethIt != g_layoutPixels.components.end() && ethCfgIt != g_config.layout.components.end()) {
        uniformData.entropyTimeSeriesEnabled = ethCfgIt->second.enabled ? 1 : 0;
        uniformData.entropyTimeSeriesX = ethIt->second.x;
        uniformData.entropyTimeSeriesY = ethIt->second.y;
        uniformData.entropyTimeSeriesWidth = ethIt->second.width;
        uniformData.entropyTimeSeriesHeight = ethIt->second.height;
    } else {
        uniformData.entropyTimeSeriesEnabled = 0;
        uniformData.entropyTimeSeriesX = 0;
        uniformData.entropyTimeSeriesY = 0;
        uniformData.entropyTimeSeriesWidth = 0;
        uniformData.entropyTimeSeriesHeight = 0;
    }

    uniformData.entropyCurrentValue = entropyNormalized;
    uniformData.entropyThreshold = entropyThreshold;
    uniformData.entropyBloomStrength = entropyBloomStrength;
    uniformData.entropyAboveThreshold = (entropyNormalized >= entropyThreshold) ? 1 : 0;
    uniformData.entropyHistoryMax = entropyHistoryMax;
    uniformData.entropyHistoryCount = entropyHistoryCount;
    uniformData.entropyHistoryWriteIndex = entropyHistoryWriteIndex;
    uniformData.entropyPad0 = 0;
    for (int i = 0; i < 16; i++) {
        uniformData.entropyHistory[i].x = entropyHistory[i * 4 + 0];
        uniformData.entropyHistory[i].y = entropyHistory[i * 4 + 1];
        uniformData.entropyHistory[i].z = entropyHistory[i * 4 + 2];
        uniformData.entropyHistory[i].w = entropyHistory[i * 4 + 3];
    }

    auto vtsIt = g_layoutPixels.components.find("volume_time_series");
    auto vtsCfgIt = g_config.layout.components.find("volume_time_series");
    if (vtsIt != g_layoutPixels.components.end() && vtsCfgIt != g_config.layout.components.end()) {
        uniformData.volumeTimeSeriesEnabled = vtsCfgIt->second.enabled ? 1 : 0;
        uniformData.volumeTimeSeriesX = vtsIt->second.x;
        uniformData.volumeTimeSeriesY = vtsIt->second.y;
        uniformData.volumeTimeSeriesWidth = vtsIt->second.width;
        uniformData.volumeTimeSeriesHeight = vtsIt->second.height;
    } else {
        uniformData.volumeTimeSeriesEnabled = 0;
        uniformData.volumeTimeSeriesX = 0;
        uniformData.volumeTimeSeriesY = 0;
        uniformData.volumeTimeSeriesWidth = 0;
        uniformData.volumeTimeSeriesHeight = 0;
    }

    uniformData.volumeHistoryMax = volumeHistoryMax;
    uniformData.volumeHistoryCount = volumeHistoryCount;
    uniformData.volumeHistoryWriteIndex = volumeHistoryWriteIndex;
    uniformData.volumePad0 = 0;
    for (int i = 0; i < 16; i++) {
        uniformData.volumeDomainHistory[i].x = volumeDomainHistory[i * 4 + 0];
        uniformData.volumeDomainHistory[i].y = volumeDomainHistory[i * 4 + 1];
        uniformData.volumeDomainHistory[i].z = volumeDomainHistory[i * 4 + 2];
        uniformData.volumeDomainHistory[i].w = volumeDomainHistory[i * 4 + 3];

        uniformData.volumeMassHistory[i].x = volumeMassHistory[i * 4 + 0];
        uniformData.volumeMassHistory[i].y = volumeMassHistory[i * 4 + 1];
        uniformData.volumeMassHistory[i].z = volumeMassHistory[i * 4 + 2];
        uniformData.volumeMassHistory[i].w = volumeMassHistory[i * 4 + 3];
    }

    // update uniform buffer
    wgpuQueueWriteBuffer(queue, uniformBuffer, 0, &uniformData, sizeof(UniformData));
}

void GPURenderer::resetEntropyTimeSeries() {
    entropyValue = 0.0f;
    entropyNormalized = 0.0f;
    entropyHistoryWriteIndex = 0;
    entropyHistoryCount = 0;
    entropyHistoryMax = 1.0f;
    entropyHistory.fill(0.0f);

    volumeHistoryWriteIndex = 0;
    volumeHistoryCount = 0;
    volumeHistoryMax = 1.0f;
    volumeDomainHistory.fill(0.0f);
    volumeMassHistory.fill(0.0f);
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
    pipelineDesc.vertex.entryPoint = WGPU_CSTR("vs_main");
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
    fragmentState.entryPoint = WGPU_CSTR(fragmentEntry);
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
    : window(window) {

    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    uniformData = {};
    uniformData.drawTarget = g_config.rendering.target;
    uniformData.drawVelocities = g_config.rendering.showVelocityVectors ? 1 : 0;
    uniformData.velScale = g_config.rendering.velocityScale;
    uniformData.windowWidth = static_cast<float>(windowWidth);
    uniformData.windowHeight = static_cast<float>(windowHeight);
    uniformData.disableHistograms = g_config.rendering.disableHistograms ? 1 : 0;
    uniformData.entropyCurrentValue = 0.0f;
    uniformData.entropyThreshold = entropyThreshold;
    uniformData.entropyBloomStrength = entropyBloomStrength;
    uniformData.entropyAboveThreshold = 0;
    uniformData.entropyHistoryMax = 1.0f;
    uniformData.entropyHistoryCount = 0;
    uniformData.entropyHistoryWriteIndex = 0;
    uniformData.entropyPad0 = 0;
}

GPURenderer::~GPURenderer() {
    if (!initialized) return;

#ifdef WEBGPU_BACKEND_EMDAWNWEBGPU
    if (instance) wgpuInstanceProcessEvents(instance);
#else
    if (device) wgpuDeviceTick(device);
#endif

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

#ifdef __EMSCRIPTEN__
    // Browser WebGPU prefers rgba8unorm for canvases.
    surfaceFormat = WGPUTextureFormat_RGBA8Unorm;
#else
    surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
#endif
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
    if (!g_config.rendering.disableHistograms && frameCount++ % HISTOGRAM_FRAME_INTERVAL == 0) {
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
        {5.0f / 255.0f, 5.0f / 255.0f, 5.0f / 255.0f, 1.0f}
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

    // On Emscripten/WebGPU, presentation is handled by the browser frame loop.
#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(surface);
#endif

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
        const float* densityMinMax = nullptr;
        const int* histogramBins = nullptr;

        uint32_t densitySumScaled = 0;
        uint32_t fluidCellCount = 0;
        if (gpuSim.getHistogramData(readySlot, pressureMinMax, velocityMinMax, densityMinMax, histogramBins, densitySumScaled, fluidCellCount)) {
            if (densityMinMax[0] < densityMinMax[1]) {
                densityHistogramMin = densityMinMax[0];
                densityHistogramMax = densityMinMax[1];
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

            entropyValue = IRenderer::computeShannonEntropy(densityHistogramBins);
            const float maxEntropy = std::log2(static_cast<float>(IRenderer::HISTOGRAM_BINS));
            if (maxEntropy > ENTROPY_EPSILON) {
                entropyNormalized = std::max(0.0f, std::min(1.0f, entropyValue / maxEntropy));
            } else {
                entropyNormalized = 0.0f;
            }

            entropyHistory[entropyHistoryWriteIndex] = entropyNormalized;
            entropyHistoryWriteIndex = (entropyHistoryWriteIndex + 1) % ENTROPY_HISTORY_SAMPLES;
            entropyHistoryCount = std::min(entropyHistoryCount + 1, ENTROPY_HISTORY_SAMPLES);
            entropyHistoryMax = ENTROPY_EPSILON;
            for (int i = 0; i < entropyHistoryCount; i++) {
                entropyHistoryMax = std::max(entropyHistoryMax, entropyHistory[i]);
            }

            const float cellArea = simulator.cellSize * simulator.cellSize;
            const float domainVolume = static_cast<float>(fluidCellCount) * cellArea;
            const float smokeMass = (static_cast<float>(densitySumScaled) / VOLUME_DENSITY_SCALE) * cellArea;

            volumeDomainHistory[volumeHistoryWriteIndex] = domainVolume;
            volumeMassHistory[volumeHistoryWriteIndex] = smokeMass;
            volumeHistoryWriteIndex = (volumeHistoryWriteIndex + 1) % VOLUME_HISTORY_SAMPLES;
            volumeHistoryCount = std::min(volumeHistoryCount + 1, VOLUME_HISTORY_SAMPLES);
            volumeHistoryMax = ENTROPY_EPSILON;
            for (int i = 0; i < volumeHistoryCount; i++) {
                volumeHistoryMax = std::max(volumeHistoryMax, std::max(volumeDomainHistory[i], volumeMassHistory[i]));
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

        entropyValue = IRenderer::computeShannonEntropy(densityHistogramBins);
        const float maxEntropy = std::log2(static_cast<float>(IRenderer::HISTOGRAM_BINS));
        if (maxEntropy > ENTROPY_EPSILON) {
            entropyNormalized = std::max(0.0f, std::min(1.0f, entropyValue / maxEntropy));
        } else {
            entropyNormalized = 0.0f;
        }

        entropyHistory[entropyHistoryWriteIndex] = entropyNormalized;
        entropyHistoryWriteIndex = (entropyHistoryWriteIndex + 1) % ENTROPY_HISTORY_SAMPLES;
        entropyHistoryCount = std::min(entropyHistoryCount + 1, ENTROPY_HISTORY_SAMPLES);
        entropyHistoryMax = ENTROPY_EPSILON;
        for (int i = 0; i < entropyHistoryCount; i++) {
            entropyHistoryMax = std::max(entropyHistoryMax, entropyHistory[i]);
        }

        const auto& density = simulator.getDensity();
        const auto& solid = simulator.getSolid();
        uint32_t fluidCells = 0;
        double densitySum = 0.0;
        const int totalCells = simulator.gridX * simulator.gridY;
        for (int i = 0; i < totalCells && i < static_cast<int>(density.size()) && i < static_cast<int>(solid.size()); i++) {
            if (solid[i] != 0.0f) {
                fluidCells++;
                const float d = std::max(0.0f, std::min(1.0f, density[i]));
                densitySum += static_cast<double>(d);
            }
        }

        const float cellArea = simulator.cellSize * simulator.cellSize;
        const float domainVolume = static_cast<float>(fluidCells) * cellArea;
        const float smokeMass = static_cast<float>(densitySum) * cellArea;

        volumeDomainHistory[volumeHistoryWriteIndex] = domainVolume;
        volumeMassHistory[volumeHistoryWriteIndex] = smokeMass;
        volumeHistoryWriteIndex = (volumeHistoryWriteIndex + 1) % VOLUME_HISTORY_SAMPLES;
        volumeHistoryCount = std::min(volumeHistoryCount + 1, VOLUME_HISTORY_SAMPLES);
        volumeHistoryMax = ENTROPY_EPSILON;
        for (int i = 0; i < volumeHistoryCount; i++) {
            volumeHistoryMax = std::max(volumeHistoryMax, std::max(volumeDomainHistory[i], volumeMassHistory[i]));
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

    WGPURequestAdapterOptions adapterOptions = {};
    adapterOptions.nextInChain = nullptr;
    adapterOptions.compatibleSurface = surface;
    adapterOptions.powerPreference = WGPUPowerPreference_HighPerformance;

#ifdef WEBGPU_BACKEND_EMDAWNWEBGPU
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void* userdata2) {
        (void)userdata2;
        UserData* userData = static_cast<UserData*>(userdata1);
        if (status == WGPURequestAdapterStatus_Success) {
            userData->adapter = adapter;
        } else {
            std::cerr << "ERR getting WebGPU adapter: " << std::string(message.data, message.length) << std::endl;
        }
        userData->requestEnded = true;
    };

    WGPURequestAdapterCallbackInfo adapterCallbackInfo = {};
    adapterCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    adapterCallbackInfo.callback = onAdapterRequestEnded;
    adapterCallbackInfo.userdata1 = &userData;
    wgpuInstanceRequestAdapter(instance, &adapterOptions, adapterCallbackInfo);
#else
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, char const* message, void* userdata) {
        UserData* userData = static_cast<UserData*>(userdata);
        if (status == WGPURequestAdapterStatus_Success) {
            userData->adapter = adapter;
        } else {
            std::cerr << "ERR getting WebGPU adapter: " << message << std::endl;
        }
        userData->requestEnded = true;
    };

    wgpuInstanceRequestAdapter(instance, &adapterOptions, onAdapterRequestEnded, &userData);
#endif

    while (!userData.requestEnded) {
#ifdef WEBGPU_BACKEND_EMDAWNWEBGPU
        // Keep pumping callback events on web builds while waiting.
        wgpuInstanceProcessEvents(instance);
#endif
#ifdef __EMSCRIPTEN__
        // Yield so the browser main thread stays responsive.
        emscripten_sleep(0);
#endif
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

    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_EMDAWNWEBGPU
    // error callback is part of device descriptor in emdawnwebgpu
    deviceDesc.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const* dev, WGPUErrorType type, WGPUStringView message, void* userdata1, void* userdata2) {
        (void)dev; (void)userdata1; (void)userdata2;
        std::cerr << "WebGPU ERR: " << type << " - " << (message.data ? std::string(message.data, message.length) : "[NO MESSAGE]") << std::endl;
    };

    auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void* userdata2) {
        (void)userdata2;
        DeviceData* deviceData = static_cast<DeviceData*>(userdata1);
        if (status == WGPURequestDeviceStatus_Success) {
            deviceData->device = device;
        } else {
            std::cerr << "ERR getting WebGPU device: " << std::string(message.data, message.length) << std::endl;
        }
        deviceData->requestEnded = true;
    };

    WGPURequestDeviceCallbackInfo deviceCallbackInfo = {};
    deviceCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    deviceCallbackInfo.callback = onDeviceRequestEnded;
    deviceCallbackInfo.userdata1 = &deviceData;
    wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCallbackInfo);
#else
    auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status, WGPUDevice device, char const* message, void* userdata) {
        DeviceData* deviceData = static_cast<DeviceData*>(userdata);
        if (status == WGPURequestDeviceStatus_Success) {
            deviceData->device = device;
        } else {
            std::cerr << "ERR getting WebGPU device: " << message << std::endl;
        }
        deviceData->requestEnded = true;
    };

    wgpuAdapterRequestDevice(adapter, &deviceDesc, onDeviceRequestEnded, &deviceData);
#endif

    while (!deviceData.requestEnded) {
#ifdef WEBGPU_BACKEND_EMDAWNWEBGPU
        // Keep pumping callback events on web builds while waiting.
        wgpuInstanceProcessEvents(instance);
#endif
#ifdef __EMSCRIPTEN__
        // Yield so the browser main thread stays responsive.
        emscripten_sleep(0);
#endif
    }

    if (!deviceData.device) {
        std::cerr << "ERR getting WebGPU device" << std::endl;
        return false;
    }

    device = deviceData.device;
    queue = wgpuDeviceGetQueue(device);

#ifndef WEBGPU_BACKEND_EMDAWNWEBGPU
    // error callback required (in emdawnwebgpu, this is set via device descriptor)
    wgpuDeviceSetUncapturedErrorCallback(device,
        [](WGPUErrorType type, const char* message, void* userdata) {
            std::cerr << "WebGPU ERR: " << type << " - " << (message ? message : "[NO MESSAGE]") << std::endl;
        }, nullptr);
#endif

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