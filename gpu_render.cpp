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

static uint32_t floatToOrderedUint(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    if (bits & 0x80000000u) {
        return ~bits;
    }
    return bits ^ 0x80000000u;
}

static float orderedUintToFloat(uint32_t ordered) {
    uint32_t bits = (ordered & 0x80000000u) ? (ordered ^ 0x80000000u) : ~ordered;
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

WebGPURenderer::WebGPURenderer(SDL_Window* window, const Config& config)
    : window(window),
      windowWidth(0),
      windowHeight(0),
      instance(nullptr),
      surface(nullptr),
      adapter(nullptr),
      device(nullptr),
      queue(nullptr),
      renderPipeline(nullptr),
      renderPipelineGPU(nullptr),
      uniformBindGroup(nullptr),
      uniformBindGroupGPU(nullptr),
      bindGroupLayout(nullptr),
      bindGroupLayoutGPU(nullptr),
      computePipelineMinMax(nullptr),
      bindGroupMinMax(nullptr),
      bindGroupLayoutMinMax(nullptr),
      uniformBuffer(nullptr),
      pressureTexture(nullptr),
      densityTexture(nullptr),
      velocityTexture(nullptr),
      solidTexture(nullptr),
      redInkTexture(nullptr),
      greenInkTexture(nullptr),
      blueInkTexture(nullptr),
      sampler(nullptr),
      pressureTextureView(nullptr),
      pressureTextureStorageView(nullptr),
      densityTextureView(nullptr),
      velocityTextureView(nullptr),
      solidTextureView(nullptr),
      solidTextureStorageView(nullptr),
      redInkTextureView(nullptr),
      greenInkTextureView(nullptr),
      blueInkTextureView(nullptr),
      minMaxBuffer(nullptr),
      minMaxStagingBuffer(nullptr),
      histogramBinBuffer(nullptr),
      histogramStagingBuffer(nullptr),
      minMaxUniformBuffer(nullptr),
      computePipelineHistogramBins(nullptr),
      bindGroupLayoutHistogramBins(nullptr),
      bindGroupHistogramBins(nullptr),
      initialized(false),
      drawTarget(config.rendering.target),
      showVelocityVectors(config.rendering.showVelocityVectors),
      disableHistograms(config.rendering.disableHistograms),
      velocityScale(config.rendering.velocityScale),
      usingGPUTextures(false),
      frameCount(0),
      densityHistogramBins(IRenderer::HISTOGRAM_BINS, 0),
      densityHistogramMin(0.0f),
      densityHistogramMax(0.0f),
      densityHistogramMaxCount(0),
      velocityHistogramBins(IRenderer::HISTOGRAM_BINS, 0),
      velocityHistogramMin(0.0f),
      velocityHistogramMax(0.0f),
      velocityHistogramMaxCount(0),
      pendingPressureMinMax{0.0f, 0.0f},
      pendingVelocityMinMax{0.0f, 0.0f},
      minMaxReadPending(false),
      minMaxMapInFlight(false),
      histogramBinsReadPending(false),
      histogramMapInFlight(false) {

    std::fill_n(pendingHistogramBins, 128, 0);
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    uniformData = {};
    uniformData.drawTarget = drawTarget;
    uniformData.drawVelocities = showVelocityVectors ? 1 : 0;
    uniformData.velScale = velocityScale;
    uniformData.windowWidth = static_cast<float>(windowWidth);
    uniformData.windowHeight = static_cast<float>(windowHeight);
    uniformData.disableHistograms = disableHistograms ? 1 : 0;
}

WebGPURenderer::~WebGPURenderer() {
    if (!initialized) return;

    releaseResources();
    initialized = false;
}

// HOLY
// BOILERPLATE
// !!!

bool WebGPURenderer::init(const Config& config) {
    if (!initWebGPU()) {
        std::cerr << "Failed to initialize WebGPU" << std::endl;
        return false;
    }

    if (!initDevice()) {
        std::cerr << "Failed to initialize device" << std::endl;
        return false;
    }

    if (!initSurface()) {
        std::cerr << "Failed to initialize surface" << std::endl;
        return false;
    }

    if (!initBuffers()) {
        std::cerr << "Failed to initialize buffers" << std::endl;
        return false;
    }

    if (!initTextures()) {
        std::cerr << "Failed to initialize textures" << std::endl;
        return false;
    }

    if (!initRenderPipeline()) {
        std::cerr << "Failed to initialize render pipeline" << std::endl;
        return false;
    }

    initialized = true;
    return true;
}


void WebGPURenderer::releaseResources() {
    // release views (before textures)
    if (pressureTextureView) {
        wgpuTextureViewRelease(pressureTextureView);
        pressureTextureView = nullptr;
    }
    if (pressureTextureStorageView) {
        wgpuTextureViewRelease(pressureTextureStorageView);
        pressureTextureStorageView = nullptr;
    }
    if (densityTextureView) {
        wgpuTextureViewRelease(densityTextureView);
        densityTextureView = nullptr;
    }
    if (velocityTextureView) {
        wgpuTextureViewRelease(velocityTextureView);
        velocityTextureView = nullptr;
    }
    if (solidTextureView) {
        wgpuTextureViewRelease(solidTextureView);
        solidTextureView = nullptr;
    }
    if (solidTextureStorageView) {
        wgpuTextureViewRelease(solidTextureStorageView);
        solidTextureStorageView = nullptr;
    }
    if (redInkTextureView) {
        wgpuTextureViewRelease(redInkTextureView);
        redInkTextureView = nullptr;
    }
    if (greenInkTextureView) {
        wgpuTextureViewRelease(greenInkTextureView);
        greenInkTextureView = nullptr;
    }
    if (blueInkTextureView) {
        wgpuTextureViewRelease(blueInkTextureView);
        blueInkTextureView = nullptr;
    }
    
    // release textures
    if (pressureTexture) {
        wgpuTextureRelease(pressureTexture);
        pressureTexture = nullptr;
    }
    if (densityTexture) {
        wgpuTextureRelease(densityTexture);
        densityTexture = nullptr;
    }
    if (velocityTexture) {
        wgpuTextureRelease(velocityTexture);
        velocityTexture = nullptr;
    }
    if (solidTexture) {
        wgpuTextureRelease(solidTexture);
        solidTexture = nullptr;
    }
    if (redInkTexture) {
        wgpuTextureRelease(redInkTexture);
        redInkTexture = nullptr;
    }
    if (greenInkTexture) {
        wgpuTextureRelease(greenInkTexture);
        greenInkTexture = nullptr;
    }
    if (blueInkTexture) {
        wgpuTextureRelease(blueInkTexture);
        blueInkTexture = nullptr;
    }
    
    // other resources
    if (renderPipeline) {
        wgpuRenderPipelineRelease(renderPipeline);
        renderPipeline = nullptr;
    }
    if (renderPipelineGPU) {
        wgpuRenderPipelineRelease(renderPipelineGPU);
        renderPipelineGPU = nullptr;
    }
    if (uniformBindGroup) {
        wgpuBindGroupRelease(uniformBindGroup);
        uniformBindGroup = nullptr;
    }
    if (uniformBindGroupGPU) {
        wgpuBindGroupRelease(uniformBindGroupGPU);
        uniformBindGroupGPU = nullptr;
    }
    if (bindGroupLayout) {
        wgpuBindGroupLayoutRelease(bindGroupLayout);
        bindGroupLayout = nullptr;
    }
    if (bindGroupLayoutGPU) {
        wgpuBindGroupLayoutRelease(bindGroupLayoutGPU);
        bindGroupLayoutGPU = nullptr;
    }
    if (bindGroupMinMax) {
        wgpuBindGroupRelease(bindGroupMinMax);
        bindGroupMinMax = nullptr;
    }
    if (bindGroupLayoutMinMax) {
        wgpuBindGroupLayoutRelease(bindGroupLayoutMinMax);
        bindGroupLayoutMinMax = nullptr;
    }
    if (computePipelineMinMax) {
        wgpuComputePipelineRelease(computePipelineMinMax);
        computePipelineMinMax = nullptr;
    }
    if (bindGroupHistogramBins) {
        wgpuBindGroupRelease(bindGroupHistogramBins);
        bindGroupHistogramBins = nullptr;
    }
    if (bindGroupLayoutHistogramBins) {
        wgpuBindGroupLayoutRelease(bindGroupLayoutHistogramBins);
        bindGroupLayoutHistogramBins = nullptr;
    }
    if (computePipelineHistogramBins) {
        wgpuComputePipelineRelease(computePipelineHistogramBins);
        computePipelineHistogramBins = nullptr;
    }
    if (sampler) {
        wgpuSamplerRelease(sampler);
        sampler = nullptr;
    }
    if (uniformBuffer) {
        wgpuBufferRelease(uniformBuffer);
        uniformBuffer = nullptr;
    }
    if (minMaxBuffer) {
        wgpuBufferRelease(minMaxBuffer);
        minMaxBuffer = nullptr;
    }
    if (minMaxStagingBuffer) {
        wgpuBufferRelease(minMaxStagingBuffer);
        minMaxStagingBuffer = nullptr;
    }
    if (histogramBinBuffer) {
        wgpuBufferRelease(histogramBinBuffer);
        histogramBinBuffer = nullptr;
    }
    if (histogramStagingBuffer) {
        wgpuBufferRelease(histogramStagingBuffer);
        histogramStagingBuffer = nullptr;
    }
    if (minMaxUniformBuffer) {
        wgpuBufferRelease(minMaxUniformBuffer);
        minMaxUniformBuffer = nullptr;
    }
    if (queue) {
        wgpuQueueRelease(queue);
        queue = nullptr;
    }
    if (device) {
        wgpuDeviceRelease(device);
        device = nullptr;
    }
    if (adapter) {
        wgpuAdapterRelease(adapter);
        adapter = nullptr;
    }
    if (surface) {
        wgpuSurfaceRelease(surface);
        surface = nullptr;
    }
    if (instance) {
        wgpuInstanceRelease(instance);
        instance = nullptr;
    }
}

bool WebGPURenderer::initWebGPU() {
    WGPUInstanceDescriptor instanceDesc = {};
    instanceDesc.nextInChain = nullptr;

    instance = wgpuCreateInstance(&instanceDesc);
    if (!instance) {
        std::cerr << "Failed to create WebGPU instance" << std::endl;
        return false;
    }

    // get surface from SDL window
    surface = SDL_GetWGPUSurface(instance, window);
    if (!surface) {
        std::cerr << "Failed to get surface from SDL window" << std::endl;
        return false;
    }

    return true;
}

bool WebGPURenderer::initDevice() {
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
            std::cerr << "Could not get WebGPU adapter: " << message << std::endl;
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
    }

    if (!userData.adapter) {
        std::cerr << "Failed to get adapter" << std::endl;
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
            std::cerr << "Could not get WebGPU device: " << message << std::endl;
        }
        deviceData->requestEnded = true;
    };

    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.nextInChain = nullptr;
    deviceDesc.label = "Katara-Device";

    wgpuAdapterRequestDevice(adapter, &deviceDesc, onDeviceRequestEnded, &deviceData);

    while (!deviceData.requestEnded) {
        // wait for device request to complete
    }

    if (!deviceData.device) {
        std::cerr << "Failed to get device" << std::endl;
        return false;
    }

    device = deviceData.device;
    queue = wgpuDeviceGetQueue(device);

    // error callback required
    wgpuDeviceSetUncapturedErrorCallback(device,
        [](WGPUErrorType type, const char* message, void* userdata) {
            std::cerr << "WebGPU error: " << type << " - " << (message ? message : "[NO MESSAGE]") << std::endl;
        }, nullptr);

    return true;
}

bool WebGPURenderer::initSurface() {
    // get the preferred format from surface
    surfaceFormat = WGPUTextureFormat_BGRA8Unorm; // fallback

    // configure surface
    WGPUSurfaceConfiguration surfaceConfig = {};
    surfaceConfig.nextInChain = nullptr;
    surfaceConfig.device = device;
    surfaceConfig.format = surfaceFormat;
    surfaceConfig.usage = WGPUTextureUsage_RenderAttachment;
    surfaceConfig.width = windowWidth;
    surfaceConfig.height = windowHeight;
    surfaceConfig.presentMode = WGPUPresentMode_Fifo;
    surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Opaque;

    wgpuSurfaceConfigure(surface, &surfaceConfig);

    return true;
}

bool WebGPURenderer::initBuffers() {
    // uniform buffer
    WGPUBufferDescriptor uniformBufferDesc = {};
    uniformBufferDesc.nextInChain = nullptr;
    uniformBufferDesc.label = "Uniform Buffer";
    uniformBufferDesc.size = sizeof(UniformData);
    uniformBufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    uniformBufferDesc.mappedAtCreation = false;

    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformBufferDesc);
    if (!uniformBuffer) {
        std::cerr << "Failed to create uniform buffer" << std::endl;
        return false;
    }

    // Min/max buffer (storage for compute shader, now 4 floats for pressure + velocity)
    WGPUBufferDescriptor minMaxBufferDesc = {};
    minMaxBufferDesc.nextInChain = nullptr;
    minMaxBufferDesc.label = "Min/Max Buffer";
    minMaxBufferDesc.size = 4 * sizeof(uint32_t);  // pressMin, pressMax, velMin, velMax
    minMaxBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    minMaxBufferDesc.mappedAtCreation = false;

    minMaxBuffer = wgpuDeviceCreateBuffer(device, &minMaxBufferDesc);
    if (!minMaxBuffer) {
        std::cerr << "Failed to create min/max buffer" << std::endl;
        return false;
    }

    // Staging buffer for readback
    WGPUBufferDescriptor stagingBufferDesc = {};
    stagingBufferDesc.nextInChain = nullptr;
    stagingBufferDesc.label = "Min/Max Staging Buffer";
    stagingBufferDesc.size = 4 * sizeof(uint32_t);
    stagingBufferDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    stagingBufferDesc.mappedAtCreation = false;

    minMaxStagingBuffer = wgpuDeviceCreateBuffer(device, &stagingBufferDesc);
    if (!minMaxStagingBuffer) {
        std::cerr << "Failed to create min/max staging buffer" << std::endl;
        return false;
    }

    // Histogram bin buffer (128 ints: 64 density + 64 velocity bins)
    WGPUBufferDescriptor histBinBufferDesc = {};
    histBinBufferDesc.nextInChain = nullptr;
    histBinBufferDesc.label = "Histogram Bin Buffer";
    histBinBufferDesc.size = 128 * sizeof(int32_t);
    histBinBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    histBinBufferDesc.mappedAtCreation = false;

    histogramBinBuffer = wgpuDeviceCreateBuffer(device, &histBinBufferDesc);
    if (!histogramBinBuffer) {
        std::cerr << "Failed to create histogram bin buffer" << std::endl;
        return false;
    }

    // Histogram staging buffer (for bin readback)
    WGPUBufferDescriptor histStagingBufferDesc = {};
    histStagingBufferDesc.nextInChain = nullptr;
    histStagingBufferDesc.label = "Histogram Staging Buffer";
    histStagingBufferDesc.size = 128 * sizeof(int32_t);
    histStagingBufferDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    histStagingBufferDesc.mappedAtCreation = false;

    histogramStagingBuffer = wgpuDeviceCreateBuffer(device, &histStagingBufferDesc);
    if (!histogramStagingBuffer) {
        std::cerr << "Failed to create histogram staging buffer" << std::endl;
        return false;
    }

    // Small uniform buffer for passing min/max to histogram bin shader
    WGPUBufferDescriptor minMaxUniformBufferDesc = {};
    minMaxUniformBufferDesc.nextInChain = nullptr;
    minMaxUniformBufferDesc.label = "Min/Max Uniform Buffer";
    minMaxUniformBufferDesc.size = 4 * sizeof(float);  // pressMin, pressMax, velMin, velMax
    minMaxUniformBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    minMaxUniformBufferDesc.mappedAtCreation = false;

    minMaxUniformBuffer = wgpuDeviceCreateBuffer(device, &minMaxUniformBufferDesc);
    if (!minMaxUniformBuffer) {
        std::cerr << "Failed to create min/max uniform buffer" << std::endl;
        return false;
    }

    return true;
}

bool WebGPURenderer::initTextures() {
    // sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.nextInChain = nullptr;
    samplerDesc.label = "Fluid Sampler";
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Nearest;
    samplerDesc.minFilter = WGPUFilterMode_Nearest;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.lodMinClamp = 0.0f;
    samplerDesc.lodMaxClamp = 32.0f;
    samplerDesc.maxAnisotropy = 1;

    sampler = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!sampler) {
        std::cerr << "Failed to create sampler" << std::endl;
        return false;
    }

    // actual textures created in updateSimulationTextures
    // (need to know grid dimensions)

    return true;
}

WGPUShaderModule WebGPURenderer::loadShader(const char* source) {
    WGPUShaderModuleWGSLDescriptor shaderCodeDesc = {};
    shaderCodeDesc.chain.next = nullptr;
    shaderCodeDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    shaderCodeDesc.code = source;

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &shaderCodeDesc.chain;
    shaderDesc.label = "Fluid Shader";

    return wgpuDeviceCreateShaderModule(device, &shaderDesc);
}


bool WebGPURenderer::initRenderPipeline() {
    std::string vertexCode = ConfigLoader::readFile("vertex.wgsl");
    std::string fragmentCode = ConfigLoader::readFile("fragment.wgsl");

    if (vertexCode.empty() || fragmentCode.empty()) {
        std::cerr << "Failed to load shader files" << std::endl;
        return false;
    }

    WGPUShaderModule vertexShader = loadShader(vertexCode.c_str());
    WGPUShaderModule fragmentShader = loadShader(fragmentCode.c_str());

    if (!vertexShader || !fragmentShader) {
        std::cerr << "Failed to load shaders" << std::endl;
        return false;
    }

    // CPU render pipeline
    // bind group layout
    std::vector<WGPUBindGroupLayoutEntry> layoutEntries = {
        // uniform buffer
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(UniformData)
            },
            .sampler = {},
            .texture = {},
            .storageTexture = {}
        },
        // sampler
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {
                .type = WGPUSamplerBindingType_NonFiltering
            },
            .texture = {},
            .storageTexture = {}
        },
        // pressure texture
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {},
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            },
            .storageTexture = {}
        },
        // density texture
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {},
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            },
            .storageTexture = {}
        },
        // velocity texture
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {},
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            },
            .storageTexture = {}
        },
        // solid texture (obstacles)
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {},
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            },
            .storageTexture = {}
        },
        // red ink texture
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {},
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            },
            .storageTexture = {}
        },
        // green ink texture
        {
            .binding = 7,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {},
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            },
            .storageTexture = {}
        },
        // blue ink texture
        {
            .binding = 8,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {},
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            },
            .storageTexture = {}
        },
    };

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
    bindGroupLayoutDesc.nextInChain = nullptr;
    bindGroupLayoutDesc.label = "Bind Group Layout";
    bindGroupLayoutDesc.entryCount = layoutEntries.size();
    bindGroupLayoutDesc.entries = layoutEntries.data();

    bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);
    if (!bindGroupLayout) {
        std::cerr << "Failed to create bind group layout" << std::endl;
        return false;
    }

    // pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.nextInChain = nullptr;
    pipelineLayoutDesc.label = "Pipeline Layout";
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);
    if (!pipelineLayout) {
        std::cerr << "Failed to create pipeline layout" << std::endl;
        return false;
    }

    // render pipeline
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = surfaceFormat;
    colorTarget.blend = nullptr;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = fragmentShader;
    fragmentState.entryPoint = "fs_main";
    fragmentState.constantCount = 0;
    fragmentState.constants = nullptr;
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.nextInChain = nullptr;
    pipelineDesc.label = "Fluid Render Pipeline";
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex = {
        .module = vertexShader,
        .entryPoint = "vs_main",
        .constantCount = 0,
        .constants = nullptr,
        .bufferCount = 0,
        .buffers = nullptr
    };
    pipelineDesc.primitive = {
        .topology = WGPUPrimitiveTopology_TriangleList,
        .stripIndexFormat = WGPUIndexFormat_Undefined,
        .frontFace = WGPUFrontFace_CCW,
        .cullMode = WGPUCullMode_None
    };
    pipelineDesc.multisample = {
        .count = 1,
        .mask = ~0u,
        .alphaToCoverageEnabled = false
    };
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.depthStencil = nullptr;

    renderPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    if (!renderPipeline) {
        std::cerr << "Failed to create render pipeline" << std::endl;
        return false;
    }

    // clean up temporary objects
    wgpuShaderModuleRelease(vertexShader);
    wgpuShaderModuleRelease(fragmentShader);
    wgpuPipelineLayoutRelease(pipelineLayout);

    // GPU render pipeline
    std::string vertexCodeGpu = ConfigLoader::readFile("vertex.wgsl");
    std::string fragmentCodeGpu = ConfigLoader::readFile("fragment_gpu.wgsl");

    if (vertexCodeGpu.empty() || fragmentCodeGpu.empty()) {
        std::cerr << "Failed to load GPU shader files" << std::endl;
        return false;
    }

    WGPUShaderModule vertexShaderGpu = loadShader(vertexCodeGpu.c_str());
    WGPUShaderModule fragmentShaderGpu = loadShader(fragmentCodeGpu.c_str());

    if (!vertexShaderGpu || !fragmentShaderGpu) {
        std::cerr << "Failed to load GPU shaders" << std::endl;
        return false;
    }

    // GPU bind group layout - single RGBA ink texture instead of 3x R32
    std::vector<WGPUBindGroupLayoutEntry> gpuLayoutEntries = {
        // uniform buffer
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(UniformData)
            },
            .sampler = {},
            .texture = {},
            .storageTexture = {}
        },
        // sampler
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {
                .type = WGPUSamplerBindingType_NonFiltering
            },
            .texture = {},
            .storageTexture = {}
        },
        // pressure texture
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {},
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            },
            .storageTexture = {}
        },
        // density texture
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {},
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            },
            .storageTexture = {}
        },
        // velocity texture
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {},
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            },
            .storageTexture = {}
        },
        // solid texture
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {},
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            },
            .storageTexture = {}
        },
        // RGBA ink texture
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {},
            .sampler = {},
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            },
            .storageTexture = {}
        }
    };

    WGPUBindGroupLayoutDescriptor gpuBindGroupLayoutDesc = {};
    gpuBindGroupLayoutDesc.nextInChain = nullptr;
    gpuBindGroupLayoutDesc.label = "GPU Bind Group Layout";
    gpuBindGroupLayoutDesc.entryCount = gpuLayoutEntries.size();
    gpuBindGroupLayoutDesc.entries = gpuLayoutEntries.data();

    bindGroupLayoutGPU = wgpuDeviceCreateBindGroupLayout(device, &gpuBindGroupLayoutDesc);
    if (!bindGroupLayoutGPU) {
        std::cerr << "Failed to create GPU bind group layout" << std::endl;
        return false;
    }

    // GPU pipeline layout
    WGPUPipelineLayoutDescriptor gpuPipelineLayoutDesc = {};
    gpuPipelineLayoutDesc.nextInChain = nullptr;
    gpuPipelineLayoutDesc.label = "GPU Pipeline Layout";
    gpuPipelineLayoutDesc.bindGroupLayoutCount = 1;
    gpuPipelineLayoutDesc.bindGroupLayouts = &bindGroupLayoutGPU;

    WGPUPipelineLayout gpuPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &gpuPipelineLayoutDesc);
    if (!gpuPipelineLayout) {
        std::cerr << "Failed to create GPU pipeline layout" << std::endl;
        return false;
    }

    // GPU render pipeline
    WGPUColorTargetState gpuColorTarget = {};
    gpuColorTarget.format = surfaceFormat;
    gpuColorTarget.blend = nullptr;
    gpuColorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState gpuFragmentState = {};
    gpuFragmentState.module = fragmentShaderGpu;
    gpuFragmentState.entryPoint = "fs_main";
    gpuFragmentState.constantCount = 0;
    gpuFragmentState.constants = nullptr;
    gpuFragmentState.targetCount = 1;
    gpuFragmentState.targets = &gpuColorTarget;

    WGPURenderPipelineDescriptor gpuPipelineDesc = {};
    gpuPipelineDesc.nextInChain = nullptr;
    gpuPipelineDesc.label = "GPU Fluid Render Pipeline";
    gpuPipelineDesc.layout = gpuPipelineLayout;
    gpuPipelineDesc.vertex = {
        .module = vertexShaderGpu,
        .entryPoint = "vs_main",
        .constantCount = 0,
        .constants = nullptr,
        .bufferCount = 0,
        .buffers = nullptr
    };
    gpuPipelineDesc.primitive = {
        .topology = WGPUPrimitiveTopology_TriangleList,
        .stripIndexFormat = WGPUIndexFormat_Undefined,
        .frontFace = WGPUFrontFace_CCW,
        .cullMode = WGPUCullMode_None
    };
    gpuPipelineDesc.multisample = {
        .count = 1,
        .mask = ~0u,
        .alphaToCoverageEnabled = false
    };
    gpuPipelineDesc.fragment = &gpuFragmentState;
    gpuPipelineDesc.depthStencil = nullptr;

    renderPipelineGPU = wgpuDeviceCreateRenderPipeline(device, &gpuPipelineDesc);
    if (!renderPipelineGPU) {
        std::cerr << "Failed to create GPU render pipeline" << std::endl;
        return false;
    }

    // clean up temporary objects
    wgpuShaderModuleRelease(vertexShaderGpu);
    wgpuShaderModuleRelease(fragmentShaderGpu);
    wgpuPipelineLayoutRelease(gpuPipelineLayout);

    // Min/max compute pipeline (now includes velocity)
    std::string minMaxCode = ConfigLoader::readFile("compute_pressure_minmax.wgsl");
    WGPUShaderModule minMaxShader = loadShader(minMaxCode.c_str());
    if (!minMaxShader) {
        std::cerr << "Failed to load min/max shader" << std::endl;
        return false;
    }

    // Bind group layout for min/max (updated to include velocity and solid textures)
    std::vector<WGPUBindGroupLayoutEntry> minMaxLayoutEntries = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {.type = WGPUBufferBindingType_Uniform, .hasDynamicOffset = false, .minBindingSize = sizeof(UniformData)}
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {.access = WGPUStorageTextureAccess_ReadOnly, .format = WGPUTextureFormat_R32Float, .viewDimension = WGPUTextureViewDimension_2D}
        },
        {
            .binding = 2,  // velocity texture (NEW)
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {.access = WGPUStorageTextureAccess_ReadOnly, .format = WGPUTextureFormat_RG32Float, .viewDimension = WGPUTextureViewDimension_2D}
        },
        {
            .binding = 3,  // solid texture (NEW)
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {.access = WGPUStorageTextureAccess_ReadOnly, .format = WGPUTextureFormat_R32Float, .viewDimension = WGPUTextureViewDimension_2D}
        },
        {
            .binding = 4,  // min/max buffer (CHANGED from binding 2)
            .visibility = WGPUShaderStage_Compute,
            .buffer = {.type = WGPUBufferBindingType_Storage, .hasDynamicOffset = false, .minBindingSize = 4 * sizeof(uint32_t)}
        }
    };

    WGPUBindGroupLayoutDescriptor minMaxLayoutDesc = {};
    minMaxLayoutDesc.nextInChain = nullptr;
    minMaxLayoutDesc.label = "Min/Max Bind Group Layout";
    minMaxLayoutDesc.entryCount = minMaxLayoutEntries.size();
    minMaxLayoutDesc.entries = minMaxLayoutEntries.data();

    bindGroupLayoutMinMax = wgpuDeviceCreateBindGroupLayout(device, &minMaxLayoutDesc);
    if (!bindGroupLayoutMinMax) {
        std::cerr << "Failed to create min/max bind group layout" << std::endl;
        wgpuShaderModuleRelease(minMaxShader);
        return false;
    }

    // Pipeline layout for min/max
    WGPUPipelineLayoutDescriptor minMaxPipelineLayoutDesc = {};
    minMaxPipelineLayoutDesc.nextInChain = nullptr;
    minMaxPipelineLayoutDesc.label = "Min/Max Pipeline Layout";
    minMaxPipelineLayoutDesc.bindGroupLayoutCount = 1;
    minMaxPipelineLayoutDesc.bindGroupLayouts = &bindGroupLayoutMinMax;

    WGPUPipelineLayout minMaxPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &minMaxPipelineLayoutDesc);
    if (!minMaxPipelineLayout) {
        std::cerr << "Failed to create min/max pipeline layout" << std::endl;
        wgpuShaderModuleRelease(minMaxShader);
        wgpuBindGroupLayoutRelease(bindGroupLayoutMinMax);
        return false;
    }

    // Compute pipeline for min/max
    WGPUComputePipelineDescriptor minMaxPipelineDesc = {};
    minMaxPipelineDesc.nextInChain = nullptr;
    minMaxPipelineDesc.label = "Min/Max Compute Pipeline";
    minMaxPipelineDesc.layout = minMaxPipelineLayout;
    minMaxPipelineDesc.compute.module = minMaxShader;
    minMaxPipelineDesc.compute.entryPoint = "main";
    minMaxPipelineDesc.compute.constantCount = 0;
    minMaxPipelineDesc.compute.constants = nullptr;

    computePipelineMinMax = wgpuDeviceCreateComputePipeline(device, &minMaxPipelineDesc);
    if (!computePipelineMinMax) {
        std::cerr << "Failed to create min/max compute pipeline" << std::endl;
        wgpuShaderModuleRelease(minMaxShader);
        wgpuPipelineLayoutRelease(minMaxPipelineLayout);
        wgpuBindGroupLayoutRelease(bindGroupLayoutMinMax);
        return false;
    }

    wgpuShaderModuleRelease(minMaxShader);
    wgpuPipelineLayoutRelease(minMaxPipelineLayout);

    // Histogram bin counting pipeline (NEW)
    std::string histBinsCode = ConfigLoader::readFile("compute_histogram_bins.wgsl");
    WGPUShaderModule histBinsShader = loadShader(histBinsCode.c_str());
    if (!histBinsShader) {
        std::cerr << "Failed to load histogram bins shader" << std::endl;
        return false;
    }

    // Bind group layout for histogram bin counting
    std::vector<WGPUBindGroupLayoutEntry> histBinsLayoutEntries = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {.type = WGPUBufferBindingType_Uniform, .hasDynamicOffset = false, .minBindingSize = sizeof(UniformData)}
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {.type = WGPUBufferBindingType_Uniform, .hasDynamicOffset = false, .minBindingSize = 4 * sizeof(float)}
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {.access = WGPUStorageTextureAccess_ReadOnly, .format = WGPUTextureFormat_R32Float, .viewDimension = WGPUTextureViewDimension_2D}
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {.access = WGPUStorageTextureAccess_ReadOnly, .format = WGPUTextureFormat_RG32Float, .viewDimension = WGPUTextureViewDimension_2D}
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {.access = WGPUStorageTextureAccess_ReadOnly, .format = WGPUTextureFormat_R32Float, .viewDimension = WGPUTextureViewDimension_2D}
        },
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {.type = WGPUBufferBindingType_Storage, .hasDynamicOffset = false, .minBindingSize = 128 * sizeof(int32_t)}
        }
    };

    WGPUBindGroupLayoutDescriptor histBinsLayoutDesc = {};
    histBinsLayoutDesc.nextInChain = nullptr;
    histBinsLayoutDesc.label = "Histogram Bins Bind Group Layout";
    histBinsLayoutDesc.entryCount = histBinsLayoutEntries.size();
    histBinsLayoutDesc.entries = histBinsLayoutEntries.data();

    bindGroupLayoutHistogramBins = wgpuDeviceCreateBindGroupLayout(device, &histBinsLayoutDesc);
    if (!bindGroupLayoutHistogramBins) {
        std::cerr << "Failed to create histogram bins bind group layout" << std::endl;
        wgpuShaderModuleRelease(histBinsShader);
        return false;
    }

    // Pipeline layout for histogram bins
    WGPUPipelineLayoutDescriptor histBinsPipelineLayoutDesc = {};
    histBinsPipelineLayoutDesc.nextInChain = nullptr;
    histBinsPipelineLayoutDesc.label = "Histogram Bins Pipeline Layout";
    histBinsPipelineLayoutDesc.bindGroupLayoutCount = 1;
    histBinsPipelineLayoutDesc.bindGroupLayouts = &bindGroupLayoutHistogramBins;

    WGPUPipelineLayout histBinsPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &histBinsPipelineLayoutDesc);
    if (!histBinsPipelineLayout) {
        std::cerr << "Failed to create histogram bins pipeline layout" << std::endl;
        wgpuShaderModuleRelease(histBinsShader);
        wgpuBindGroupLayoutRelease(bindGroupLayoutHistogramBins);
        return false;
    }

    // Compute pipeline for histogram bins
    WGPUComputePipelineDescriptor histBinsPipelineDesc = {};
    histBinsPipelineDesc.nextInChain = nullptr;
    histBinsPipelineDesc.label = "Histogram Bins Compute Pipeline";
    histBinsPipelineDesc.layout = histBinsPipelineLayout;
    histBinsPipelineDesc.compute.module = histBinsShader;
    histBinsPipelineDesc.compute.entryPoint = "main";
    histBinsPipelineDesc.compute.constantCount = 0;
    histBinsPipelineDesc.compute.constants = nullptr;

    computePipelineHistogramBins = wgpuDeviceCreateComputePipeline(device, &histBinsPipelineDesc);
    if (!computePipelineHistogramBins) {
        std::cerr << "Failed to create histogram bins compute pipeline" << std::endl;
        wgpuShaderModuleRelease(histBinsShader);
        wgpuPipelineLayoutRelease(histBinsPipelineLayout);
        wgpuBindGroupLayoutRelease(bindGroupLayoutHistogramBins);
        return false;
    }

    wgpuShaderModuleRelease(histBinsShader);
    wgpuPipelineLayoutRelease(histBinsPipelineLayout);

    return true;
}

void WebGPURenderer::computeHistograms(const ISimulator& simulator) {
    if (usingGPUTextures) {
        // GPU mode - use GPU-computed histogram data
        if (histogramBinsReadPending) {
            // Use unified min/max data (shared with pressure coloring)
            densityHistogramMin = pendingPressureMinMax[0];
            densityHistogramMax = pendingPressureMinMax[1];
            velocityHistogramMin = pendingVelocityMinMax[0];
            velocityHistogramMax = pendingVelocityMinMax[1];

            // Copy bins from GPU
            for (int i = 0; i < 64; i++) {
                densityHistogramBins[i] = pendingHistogramBins[i];
                velocityHistogramBins[i] = pendingHistogramBins[64 + i];
            }

            // Compute max counts
            densityHistogramMaxCount = 0;
            velocityHistogramMaxCount = 0;
            for (int i = 0; i < IRenderer::HISTOGRAM_BINS; i++) {
                densityHistogramMaxCount = std::max(densityHistogramMaxCount, densityHistogramBins[i]);
                velocityHistogramMaxCount = std::max(velocityHistogramMaxCount, velocityHistogramBins[i]);
            }

            histogramBinsReadPending = false;
        }
        // If data not ready yet, keep previous values (or zeros if first frame)
    } else {
        // CPU mode - use existing implementation
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

void WebGPURenderer::updateUniformData(const ISimulator& simulator) {
    uniformData.gridX = simulator.getGridX();
    uniformData.gridY = simulator.getGridY();
    uniformData.cellSize = simulator.getCellSize();
    uniformData.simWidth = uniformData.gridX * uniformData.cellSize;
    uniformData.simHeight = uniformData.gridY * uniformData.cellSize;

    // pressure range
    if (usingGPUTextures && minMaxReadPending) {
        // Use GPU read-back min/max values
        uniformData.pressureMin = pendingPressureMinMax[0];
        uniformData.pressureMax = pendingPressureMinMax[1];
        minMaxReadPending = false;
    } else if (usingGPUTextures) {
        // GPU mode but min/max not ready yet - avoid zero range
        if (uniformData.pressureMin == uniformData.pressureMax) {
            uniformData.pressureMin = -1.0f;
            uniformData.pressureMax = 1.0f;
        }
    } else if (!usingGPUTextures) {
        // CPU mode - calculate from CPU pressure data
        const auto& pressure = simulator.getPressure();
        if (!pressure.empty()) {
            uniformData.pressureMin = *std::min_element(pressure.begin(), pressure.end());
            uniformData.pressureMax = *std::max_element(pressure.begin(), pressure.end());
            std::cout << "CPU pressure min/max: min=" << uniformData.pressureMin
                      << ", max=" << uniformData.pressureMax << std::endl;
        }
    }

    // histogram data - use unified min/max for GPU mode
    if (usingGPUTextures) {
        uniformData.densityHistogramMin = pendingPressureMinMax[0];
        uniformData.densityHistogramMax = pendingPressureMinMax[1];
        uniformData.velocityHistogramMin = pendingVelocityMinMax[0];
        uniformData.velocityHistogramMax = pendingVelocityMinMax[1];
    } else {
        uniformData.densityHistogramMin = densityHistogramMin;
        uniformData.densityHistogramMax = densityHistogramMax;
        uniformData.velocityHistogramMin = velocityHistogramMin;
        uniformData.velocityHistogramMax = velocityHistogramMax;
    }
    uniformData.densityHistogramMaxCount = densityHistogramMaxCount;
    uniformData.velocityHistogramMaxCount = velocityHistogramMaxCount;
    // pack histogram bins into vec4 arrays 
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

void WebGPURenderer::updateSimulationTextures(const ISimulator& simulator) {
    int gridX = simulator.getGridX();
    int gridY = simulator.getGridY();

    if (simulator.isUsingGPU()) {
        usingGPUTextures = true;
    }

    // GPU MODE
    if (usingGPUTextures) {
        const auto& gpuSim = static_cast<const GPUFluidSimulator&>(simulator);

        if (!pressureTextureView || uniformData.gridX != gridX || uniformData.gridY != gridY) {
            if (pressureTextureView) wgpuTextureViewRelease(pressureTextureView);
            if (pressureTextureStorageView) wgpuTextureViewRelease(pressureTextureStorageView);
            if (densityTextureView) wgpuTextureViewRelease(densityTextureView);
            if (velocityTextureView) wgpuTextureViewRelease(velocityTextureView);
            if (solidTextureView) wgpuTextureViewRelease(solidTextureView);
            if (solidTextureStorageView) wgpuTextureViewRelease(solidTextureStorageView);

            WGPUTextureViewDescriptor viewDesc = {};
            viewDesc.nextInChain = nullptr;
            viewDesc.format = WGPUTextureFormat_R32Float;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.baseMipLevel = 0;
            viewDesc.mipLevelCount = 1;
            viewDesc.baseArrayLayer = 0;
            viewDesc.arrayLayerCount = 1;

            pressureTextureView = wgpuTextureCreateView(gpuSim.getPressureTexture(), &viewDesc);
            pressureTextureStorageView = wgpuTextureCreateView(gpuSim.getPressureTexture(), &viewDesc);
            densityTextureView = wgpuTextureCreateView(gpuSim.getDensityTexture(), &viewDesc);
            solidTextureView = wgpuTextureCreateView(gpuSim.getSolidTexture(), &viewDesc);
            solidTextureStorageView = wgpuTextureCreateView(gpuSim.getSolidTexture(), &viewDesc);

            viewDesc.format = WGPUTextureFormat_RG32Float;
            velocityTextureView = wgpuTextureCreateView(gpuSim.getVelocityTexture(), &viewDesc);

            viewDesc.format = WGPUTextureFormat_RGBA32Float;
            // reusing redInkTextureView for the combined ink texture
            if (redInkTextureView) wgpuTextureViewRelease(redInkTextureView);
            redInkTextureView = wgpuTextureCreateView(gpuSim.getInkTexture(), &viewDesc);
        }

        // recreate gpu bind group when texture views are updated
        if (uniformBindGroupGPU) {
            wgpuBindGroupRelease(uniformBindGroupGPU);
            uniformBindGroupGPU = nullptr;
        }

        // GPU bind group (to support different ink texture format)
        std::vector<WGPUBindGroupEntry> bindGroupEntries = {
            {
                .binding = 0,
                .buffer = uniformBuffer,
                .offset = 0,
                .size = sizeof(UniformData)
            },
            {
                .binding = 1,
                .sampler = sampler
            },
            {
                .binding = 2,
                .textureView = pressureTextureView
            },
            {
                .binding = 3,
                .textureView = densityTextureView
            },
            {
                .binding = 4,
                .textureView = velocityTextureView
            },
            {
                .binding = 5,
                .textureView = solidTextureView
            },
            {
                .binding = 6,
                .textureView = redInkTextureView // RGBA ink texture
            }
        };

        WGPUBindGroupDescriptor bindGroupDesc = {};
        bindGroupDesc.nextInChain = nullptr;
        bindGroupDesc.label = "GPU Bind Group";
        bindGroupDesc.layout = bindGroupLayoutGPU;
        bindGroupDesc.entryCount = bindGroupEntries.size();
        bindGroupDesc.entries = bindGroupEntries.data();

        uniformBindGroupGPU = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);
        if (!uniformBindGroupGPU) {
            std::cerr << "Failed to create GPU bind group" << std::endl;
        }

        uniformData.gridX = gridX;
        uniformData.gridY = gridY;

        return;
    }

    // CPU MODE
    // create textures initially or on resize
    if (!pressureTexture || uniformData.gridX != gridX || uniformData.gridY != gridY) {
        // release old textures (views first, then textures)
        if (pressureTextureView) {
            wgpuTextureViewRelease(pressureTextureView);
            pressureTextureView = nullptr;
        }
        if (pressureTextureStorageView) {
            wgpuTextureViewRelease(pressureTextureStorageView);
            pressureTextureStorageView = nullptr;
        }
        if (densityTextureView) {
            wgpuTextureViewRelease(densityTextureView);
            densityTextureView = nullptr;
        }
        if (velocityTextureView) {
            wgpuTextureViewRelease(velocityTextureView);
            velocityTextureView = nullptr;
        }
        if (solidTextureView) {
            wgpuTextureViewRelease(solidTextureView);
            solidTextureView = nullptr;
        }
        if (redInkTextureView) {
            wgpuTextureViewRelease(redInkTextureView);
            redInkTextureView = nullptr;
        }
        if (greenInkTextureView) {
            wgpuTextureViewRelease(greenInkTextureView);
            greenInkTextureView = nullptr;
        }
        if (blueInkTextureView) {
            wgpuTextureViewRelease(blueInkTextureView);
            blueInkTextureView = nullptr;
        }
        if (pressureTexture) {
            wgpuTextureRelease(pressureTexture);
            pressureTexture = nullptr;
        }
        if (densityTexture) {
            wgpuTextureRelease(densityTexture);
            densityTexture = nullptr;
        }
        if (velocityTexture) {
            wgpuTextureRelease(velocityTexture);
            velocityTexture = nullptr;
        }
        if (solidTexture) {
            wgpuTextureRelease(solidTexture);
            solidTexture = nullptr;
        }
        if (redInkTexture) {
            wgpuTextureRelease(redInkTexture);
            redInkTexture = nullptr;
        }
        if (greenInkTexture) {
            wgpuTextureRelease(greenInkTexture);
            greenInkTexture = nullptr;
        }
        if (blueInkTexture) {
            wgpuTextureRelease(blueInkTexture);
            blueInkTexture = nullptr;
        }

        // release old bind group before creating new textures
        if (uniformBindGroup) {
            wgpuBindGroupRelease(uniformBindGroup);
            uniformBindGroup = nullptr;
        }

        // create new textures
        WGPUTextureDescriptor textureDesc = {};
        textureDesc.nextInChain = nullptr;
        textureDesc.size = { static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1 };
        textureDesc.mipLevelCount = 1;
        textureDesc.sampleCount = 1;
        textureDesc.dimension = WGPUTextureDimension_2D;
        textureDesc.format = WGPUTextureFormat_R32Float;
        textureDesc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        textureDesc.label = "Pressure Texture";

        pressureTexture = wgpuDeviceCreateTexture(device, &textureDesc);

        textureDesc.label = "Density Texture";
        densityTexture = wgpuDeviceCreateTexture(device, &textureDesc);

        textureDesc.label = "Velocity Texture";
        textureDesc.format = WGPUTextureFormat_RG32Float; // R=X velocity, G=Y velocity
        velocityTexture = wgpuDeviceCreateTexture(device, &textureDesc);

        textureDesc.label = "Solid Texture";
        textureDesc.format = WGPUTextureFormat_R32Float; // single channel for solid/fluid
        solidTexture = wgpuDeviceCreateTexture(device, &textureDesc);

        // create ink textures with the same dimensions as other textures
        textureDesc.label = "Red Ink Texture";
        redInkTexture = wgpuDeviceCreateTexture(device, &textureDesc);

        textureDesc.label = "Green Ink Texture";
        greenInkTexture = wgpuDeviceCreateTexture(device, &textureDesc);

        textureDesc.label = "Blue Ink Texture";
        blueInkTexture = wgpuDeviceCreateTexture(device, &textureDesc);

        if (!pressureTexture || !densityTexture || !velocityTexture || !solidTexture ||
            !redInkTexture || !greenInkTexture || !blueInkTexture) {
            std::cerr << "Failed to create simulation textures" << std::endl;
            return;
        }

        // create texture views
        WGPUTextureViewDescriptor viewDesc = {};
        viewDesc.nextInChain = nullptr;
        viewDesc.format = WGPUTextureFormat_R32Float;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.baseMipLevel = 0;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;

        pressureTextureView = wgpuTextureCreateView(pressureTexture, &viewDesc);
        densityTextureView = wgpuTextureCreateView(densityTexture, &viewDesc);

        viewDesc.format = WGPUTextureFormat_RG32Float;
        velocityTextureView = wgpuTextureCreateView(velocityTexture, &viewDesc);

        viewDesc.format = WGPUTextureFormat_R32Float;
        solidTextureView = wgpuTextureCreateView(solidTexture, &viewDesc);

        // create ink texture views
        redInkTextureView = wgpuTextureCreateView(redInkTexture, &viewDesc);
        greenInkTextureView = wgpuTextureCreateView(greenInkTexture, &viewDesc);
        blueInkTextureView = wgpuTextureCreateView(blueInkTexture, &viewDesc);

        if (!pressureTextureView || !densityTextureView || !velocityTextureView || !solidTextureView ||
            !redInkTextureView || !greenInkTextureView || !blueInkTextureView) {
            std::cerr << "Failed to create texture views" << std::endl;
            return;
        }

        // create bind groups
        std::vector<WGPUBindGroupEntry> bindGroupEntries = {
            {
                .binding = 0,
                .buffer = uniformBuffer,
                .offset = 0,
                .size = sizeof(UniformData)
            },
            {
                .binding = 1,
                .sampler = sampler
            },
            {
                .binding = 2,
                .textureView = pressureTextureView
            },
            {
                .binding = 3,
                .textureView = densityTextureView
            },
            {
                .binding = 4,
                .textureView = velocityTextureView
            },
            {
                .binding = 5,
                .textureView = solidTextureView
            },
            {
                .binding = 6,
                .textureView = redInkTextureView
            },
            {
                .binding = 7,
                .textureView = greenInkTextureView
            },
            {
                .binding = 8,
                .textureView = blueInkTextureView
            }
        };

        WGPUBindGroupDescriptor bindGroupDesc = {};
        bindGroupDesc.nextInChain = nullptr;
        bindGroupDesc.label = "Main Bind Group";
        bindGroupDesc.layout = bindGroupLayout;
        bindGroupDesc.entryCount = bindGroupEntries.size();
        bindGroupDesc.entries = bindGroupEntries.data();

        uniformBindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);
        if (!uniformBindGroup) {
            std::cerr << "Failed to create bind group" << std::endl;
            return;
        }
    }

    // update texture data
    const auto& pressure = simulator.getPressure();
    const auto& density = simulator.getDensity();
    const auto& velocityX = simulator.getVelocityX();
    const auto& velocityY = simulator.getVelocityY();
    const auto& solid = simulator.getSolid();

    if (!pressure.empty()) {
        // write pressure data to texture
        WGPUImageCopyTexture pressureCopy = {
            .texture = pressureTexture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
            .aspect = WGPUTextureAspect_All
        };

        WGPUTextureDataLayout pressureLayout = {
            .offset = 0,
            .bytesPerRow = static_cast<uint32_t>(gridX * sizeof(float)),
            .rowsPerImage = static_cast<uint32_t>(gridY)
        };

        WGPUExtent3D pressureExtent = {
            .width = static_cast<uint32_t>(gridX),
            .height = static_cast<uint32_t>(gridY),
            .depthOrArrayLayers = 1
        };

        wgpuQueueWriteTexture(queue, &pressureCopy, pressure.data(),
                           pressure.size() * sizeof(float), &pressureLayout, &pressureExtent);

        // write density data to texture
        if (!density.empty()) {
            WGPUImageCopyTexture densityCopy = {
                .texture = densityTexture,
                .mipLevel = 0,
                .origin = {0, 0, 0},
                .aspect = WGPUTextureAspect_All
            };

            WGPUTextureDataLayout densityLayout = {
                .offset = 0,
                .bytesPerRow = static_cast<uint32_t>(gridX * sizeof(float)),
                .rowsPerImage = static_cast<uint32_t>(gridY)
            };

            WGPUExtent3D densityExtent = {
                .width = static_cast<uint32_t>(gridX),
                .height = static_cast<uint32_t>(gridY),
                .depthOrArrayLayers = 1
            };

            wgpuQueueWriteTexture(queue, &densityCopy, density.data(),
                               density.size() * sizeof(float), &densityLayout, &densityExtent);
        }

        // write velocity data to texture
        // uses interleaved RG
        std::vector<float> velocityData;
        velocityData.reserve(pressure.size() * 2);
        for (size_t i = 0; i < pressure.size(); ++i) {
            velocityData.push_back(velocityX[i]);
            velocityData.push_back(velocityY[i]);
        }

        if (!velocityData.empty()) {
            WGPUImageCopyTexture velocityCopy = {
                .texture = velocityTexture,
                .mipLevel = 0,
                .origin = {0, 0, 0},
                .aspect = WGPUTextureAspect_All
            };

            WGPUTextureDataLayout velocityLayout = {
                .offset = 0,
                .bytesPerRow = static_cast<uint32_t>(gridX * 2 * sizeof(float)),
                .rowsPerImage = static_cast<uint32_t>(gridY)
            };

            WGPUExtent3D velocityExtent = {
                .width = static_cast<uint32_t>(gridX),
                .height = static_cast<uint32_t>(gridY),
                .depthOrArrayLayers = 1
            };

            wgpuQueueWriteTexture(queue, &velocityCopy, velocityData.data(),
                               velocityData.size() * sizeof(float), &velocityLayout, &velocityExtent);
        }

        // write solid data to texture
        if (!solid.empty()) {
            WGPUImageCopyTexture solidCopy = {
                .texture = solidTexture,
                .mipLevel = 0,
                .origin = {0, 0, 0},
                .aspect = WGPUTextureAspect_All
            };

            WGPUTextureDataLayout solidLayout = {
                .offset = 0,
                .bytesPerRow = static_cast<uint32_t>(gridX * sizeof(float)),
                .rowsPerImage = static_cast<uint32_t>(gridY)
            };

            WGPUExtent3D solidExtent = {
                .width = static_cast<uint32_t>(gridX),
                .height = static_cast<uint32_t>(gridY),
                .depthOrArrayLayers = 1
            };

            wgpuQueueWriteTexture(queue, &solidCopy, solid.data(),
                               solid.size() * sizeof(float), &solidLayout, &solidExtent);
        }

        // write ink data to textures
        const auto& redInk = simulator.getRedInk();
        const auto& greenInk = simulator.getGreenInk();
        const auto& blueInk = simulator.getBlueInk();

        // only process those textures if the simulator has ink initialized
        if (simulator.isInkInitialized() && !redInk.empty()) {
            WGPUImageCopyTexture redInkCopy = {
                .texture = redInkTexture,
                .mipLevel = 0,
                .origin = {0, 0, 0},
                .aspect = WGPUTextureAspect_All
            };

            WGPUTextureDataLayout redInkLayout = {
                .offset = 0,
                .bytesPerRow = static_cast<uint32_t>(gridX * sizeof(float)),
                .rowsPerImage = static_cast<uint32_t>(gridY)
            };

            WGPUExtent3D redInkExtent = {
                .width = static_cast<uint32_t>(gridX),
                .height = static_cast<uint32_t>(gridY),
                .depthOrArrayLayers = 1
            };

            wgpuQueueWriteTexture(queue, &redInkCopy, redInk.data(),
                                   redInk.size() * sizeof(float), &redInkLayout, &redInkExtent);
        }

        if (!greenInk.empty()) {
            WGPUImageCopyTexture greenInkCopy = {
                .texture = greenInkTexture,
                .mipLevel = 0,
                .origin = {0, 0, 0},
                .aspect = WGPUTextureAspect_All
            };

            WGPUTextureDataLayout greenInkLayout = {
                .offset = 0,
                .bytesPerRow = static_cast<uint32_t>(gridX * sizeof(float)),
                .rowsPerImage = static_cast<uint32_t>(gridY)
            };

            WGPUExtent3D greenInkExtent = {
                .width = static_cast<uint32_t>(gridX),
                .height = static_cast<uint32_t>(gridY),
                .depthOrArrayLayers = 1
            };

            wgpuQueueWriteTexture(queue, &greenInkCopy, greenInk.data(),
                                   greenInk.size() * sizeof(float), &greenInkLayout, &greenInkExtent);
        }

        if (!blueInk.empty()) {
            WGPUImageCopyTexture blueInkCopy = {
                .texture = blueInkTexture,
                .mipLevel = 0,
                .origin = {0, 0, 0},
                .aspect = WGPUTextureAspect_All
            };

            WGPUTextureDataLayout blueInkLayout = {
                .offset = 0,
                .bytesPerRow = static_cast<uint32_t>(gridX * sizeof(float)),
                .rowsPerImage = static_cast<uint32_t>(gridY)
            };

            WGPUExtent3D blueInkExtent = {
                .width = static_cast<uint32_t>(gridX),
                .height = static_cast<uint32_t>(gridY),
                .depthOrArrayLayers = 1
            };

            wgpuQueueWriteTexture(queue, &blueInkCopy, blueInk.data(),
                                   blueInk.size() * sizeof(float), &blueInkLayout, &blueInkExtent);
        }
    }
}

void WebGPURenderer::render(const ISimulator& simulator) {
    if (!initialized) return;

    // compute histograms every n frames
    int histogramFrameInterval = 1;
    if (!disableHistograms && frameCount++ % histogramFrameInterval == 0) {
        computeHistograms(simulator);
    }

    updateUniformData(simulator);
    updateSimulationTextures(simulator);

    // In GPU mode, calculate unified min/max (pressure + velocity) on GPU
    if (usingGPUTextures && !minMaxMapInFlight) {
        const auto& gpuSim = static_cast<const GPUFluidSimulator&>(simulator);
        int gridX = simulator.getGridX();
        int gridY = simulator.getGridY();

        // Initialize min/max buffer with 4 values (pressMin, pressMax, velMin, velMax)
        const float positiveInf = std::numeric_limits<float>::infinity();
        const float negativeInf = -std::numeric_limits<float>::infinity();
        uint32_t initMinMaxData[4] = {
            floatToOrderedUint(positiveInf),   // pressMin
            floatToOrderedUint(negativeInf),   // pressMax
            floatToOrderedUint(positiveInf),   // velMin
            floatToOrderedUint(negativeInf)    // velMax
        };
        wgpuQueueWriteBuffer(queue, minMaxBuffer, 0, initMinMaxData, sizeof(initMinMaxData));

        // Create bind group for min/max compute (now includes velocity and solid textures)
        WGPUBindGroupEntry minMaxEntries[] = {
            {.binding = 0, .buffer = uniformBuffer, .offset = 0, .size = sizeof(UniformData)},
            {.binding = 1, .textureView = pressureTextureStorageView},
            {.binding = 2, .textureView = velocityTextureView},
            {.binding = 3, .textureView = solidTextureStorageView},
            {.binding = 4, .buffer = minMaxBuffer, .offset = 0, .size = 4 * sizeof(uint32_t)}
        };

        WGPUBindGroupDescriptor minMaxBGDesc = {};
        minMaxBGDesc.nextInChain = nullptr;
        minMaxBGDesc.label = "Min/Max Bind Group";
        minMaxBGDesc.layout = bindGroupLayoutMinMax;
        minMaxBGDesc.entryCount = 5;
        minMaxBGDesc.entries = minMaxEntries;

        if (bindGroupMinMax) {
            wgpuBindGroupRelease(bindGroupMinMax);
        }
        bindGroupMinMax = wgpuDeviceCreateBindGroup(device, &minMaxBGDesc);
        if (!bindGroupMinMax) {
            std::cerr << "Failed to create min/max bind group" << std::endl;
        }

        // Encode compute pass for min/max calculation
        WGPUCommandEncoderDescriptor encoderDesc = {};
        encoderDesc.nextInChain = nullptr;
        encoderDesc.label = "Min/Max Compute Encoder";

        WGPUCommandEncoder computeEncoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

        WGPUComputePassDescriptor computePassDesc = {};
        computePassDesc.nextInChain = nullptr;

        WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(computeEncoder, &computePassDesc);

        wgpuComputePassEncoderSetPipeline(computePass, computePipelineMinMax);
        wgpuComputePassEncoderSetBindGroup(computePass, 0, bindGroupMinMax, 0, nullptr);

        // Dispatch workgroups
        wgpuComputePassEncoderDispatchWorkgroups(computePass,
            (gridX + 15) / 16,
            (gridY + 15) / 16,
            1);

        wgpuComputePassEncoderEnd(computePass);
        wgpuComputePassEncoderRelease(computePass);

        // Copy 4 floats to staging buffer for readback
        wgpuCommandEncoderCopyBufferToBuffer(computeEncoder,
            minMaxBuffer,
            0,  // source offset
            minMaxStagingBuffer,
            0,  // destination offset
            4 * sizeof(uint32_t));

        // Finish and submit commands
        WGPUCommandBufferDescriptor cmdDesc = {};
        cmdDesc.nextInChain = nullptr;
        cmdDesc.label = "Min/Max Commands";

        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(computeEncoder, &cmdDesc);
        wgpuCommandEncoderRelease(computeEncoder);

        wgpuQueueSubmit(queue, 1, &commands);
        wgpuCommandBufferRelease(commands);

        // Map staging buffer asynchronously for readback
        minMaxMapInFlight = true;
        WGPUBufferMapCallbackInfo2 mapCallbackInfo = {};
        mapCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        mapCallbackInfo.callback = [](WGPUMapAsyncStatus status, const char* message, void* userdata1, void* userdata2) {
            (void)message; (void)userdata2;
            WebGPURenderer* renderer = static_cast<WebGPURenderer*>(userdata1);
            if (status == WGPUMapAsyncStatus_Success) {
                const uint32_t* data = static_cast<const uint32_t*>(
                    wgpuBufferGetConstMappedRange(renderer->minMaxStagingBuffer, 0, 4 * sizeof(uint32_t))
                );
                if (data) {
                    // Read 4 values: pressure min/max + velocity min/max
                    renderer->pendingPressureMinMax[0] = orderedUintToFloat(data[0]);  // pressMin
                    renderer->pendingPressureMinMax[1] = orderedUintToFloat(data[1]);  // pressMax
                    renderer->pendingVelocityMinMax[0] = orderedUintToFloat(data[2]);  // velMin
                    renderer->pendingVelocityMinMax[1] = orderedUintToFloat(data[3]);  // velMax
                    std::cout << "GPU pressure min/max: min=" << renderer->pendingPressureMinMax[0]
                              << ", max=" << renderer->pendingPressureMinMax[1] << std::endl;
                    renderer->minMaxReadPending = true;

                    // Trigger histogram bin counting
                    renderer->dispatchHistogramBinCounting();
                }
                wgpuBufferUnmap(renderer->minMaxStagingBuffer);
            } else {
                renderer->minMaxMapInFlight = false;
            }
        };
        mapCallbackInfo.userdata1 = this;
        mapCallbackInfo.userdata2 = nullptr;
        wgpuBufferMapAsync2(minMaxStagingBuffer, WGPUMapMode_Read, 0, 4 * sizeof(uint32_t),
            mapCallbackInfo);
    }

    // get current texture from surface
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);

    // check if surface is still valid
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        std::cerr << "Surface texture status error: " << surfaceTexture.status << std::endl;
        if (surfaceTexture.texture) {
            wgpuTextureRelease(surfaceTexture.texture);
        }
        return;
    }

    if (!surfaceTexture.texture) {
        std::cerr << "Failed to get texture from surface" << std::endl;
        return;
    }

    // try to get the texture view directly from the surface texture
    WGPUTextureView nextTexture = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
    if (!nextTexture) {
        std::cerr << "Failed to create texture view from surface texture" << std::endl;
        wgpuTextureRelease(surfaceTexture.texture);
        return;
    }

    // command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain = nullptr;
    encoderDesc.label = "Command Encoder";

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
    if (!encoder) {
        std::cerr << "Failed to create command encoder" << std::endl;
        wgpuTextureViewRelease(nextTexture);
        wgpuTextureRelease(surfaceTexture.texture);
        return;
    }

    // render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = nextTexture;
    colorAttachment.resolveTarget = nullptr;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED; // https://github.com/floooh/sokol/issues/1003
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = { 0.0f, 0.0f, 0.0f, 1.0f };

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.nextInChain = nullptr;
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;

    WGPURenderPassEncoder renderPassEncoder = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    if (!renderPassEncoder) {
        std::cerr << "Failed to begin render pass" << std::endl;
        wgpuTextureViewRelease(nextTexture);
        wgpuTextureRelease(surfaceTexture.texture);
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
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    cmdBufferDesc.nextInChain = nullptr;
    cmdBufferDesc.label = "Command Buffer";

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(queue, 1, &commands);

    // present (draw)
    wgpuSurfacePresent(surface);

    // clean up
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(nextTexture);
    wgpuTextureRelease(surfaceTexture.texture);
    wgpuInstanceProcessEvents(instance);
}

void WebGPURenderer::dispatchHistogramBinCounting() {
    if (!usingGPUTextures) return;

    int gridX = uniformData.gridX;
    int gridY = uniformData.gridY;

    // Initialize bin buffer to zeros
    int32_t zeroBins[128] = {0};
    wgpuQueueWriteBuffer(queue, histogramBinBuffer, 0, zeroBins, sizeof(zeroBins));

    // Update min/max uniform buffer with unified min/max data
    float minMaxUniform[4] = {
        pendingPressureMinMax[0],  // pressMin (from unified min/max)
        pendingPressureMinMax[1],  // pressMax (from unified min/max)
        pendingVelocityMinMax[0],  // velMin (from unified min/max)
        pendingVelocityMinMax[1]   // velMax (from unified min/max)
    };
    wgpuQueueWriteBuffer(queue, minMaxUniformBuffer, 0, minMaxUniform, sizeof(minMaxUniform));

    // Create bind group for bin counting
    WGPUBindGroupEntry histBinsEntries[] = {
        {.binding = 0, .buffer = uniformBuffer, .offset = 0, .size = sizeof(UniformData)},
        {.binding = 1, .buffer = minMaxUniformBuffer, .offset = 0, .size = 4 * sizeof(float)},
        {.binding = 2, .textureView = pressureTextureStorageView},
        {.binding = 3, .textureView = velocityTextureView},
        {.binding = 4, .textureView = solidTextureStorageView},
        {.binding = 5, .buffer = histogramBinBuffer, .offset = 0, .size = 128 * sizeof(int32_t)}
    };

    WGPUBindGroupDescriptor histBinsBGDesc = {};
    histBinsBGDesc.nextInChain = nullptr;
    histBinsBGDesc.label = "Histogram Bins Bind Group";
    histBinsBGDesc.layout = bindGroupLayoutHistogramBins;
    histBinsBGDesc.entryCount = 6;
    histBinsBGDesc.entries = histBinsEntries;

    if (bindGroupHistogramBins) {
        wgpuBindGroupRelease(bindGroupHistogramBins);
    }
    bindGroupHistogramBins = wgpuDeviceCreateBindGroup(device, &histBinsBGDesc);

    // Encode compute pass for bin counting
    WGPUCommandEncoderDescriptor binEncoderDesc = {};
    binEncoderDesc.nextInChain = nullptr;
    binEncoderDesc.label = "Histogram Bins Encoder";
    WGPUCommandEncoder binEncoder = wgpuDeviceCreateCommandEncoder(device, &binEncoderDesc);

    WGPUComputePassDescriptor binPassDesc = {};
    binPassDesc.nextInChain = nullptr;
    WGPUComputePassEncoder binPass = wgpuCommandEncoderBeginComputePass(binEncoder, &binPassDesc);

    wgpuComputePassEncoderSetPipeline(binPass, computePipelineHistogramBins);
    wgpuComputePassEncoderSetBindGroup(binPass, 0, bindGroupHistogramBins, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(binPass, (gridX + 15) / 16, (gridY + 15) / 16, 1);

    wgpuComputePassEncoderEnd(binPass);
    wgpuComputePassEncoderRelease(binPass);

    // Copy bins to staging for readback
    wgpuCommandEncoderCopyBufferToBuffer(binEncoder,
        histogramBinBuffer, 0,
        histogramStagingBuffer, 0,
        128 * sizeof(int32_t));

    WGPUCommandBufferDescriptor binCmdDesc = {};
    binCmdDesc.nextInChain = nullptr;
    binCmdDesc.label = "Histogram Bins Commands";
    WGPUCommandBuffer binCommands = wgpuCommandEncoderFinish(binEncoder, &binCmdDesc);
    wgpuCommandEncoderRelease(binEncoder);

    wgpuQueueSubmit(queue, 1, &binCommands);
    wgpuCommandBufferRelease(binCommands);

    // Map staging buffer for bins
    WGPUBufferMapCallbackInfo2 binMapCallbackInfo = {};
    binMapCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    binMapCallbackInfo.callback = [](WGPUMapAsyncStatus status, const char* message, void* userdata1, void* userdata2) {
        (void)message; (void)userdata2;
        WebGPURenderer* renderer = static_cast<WebGPURenderer*>(userdata1);
        if (status == WGPUMapAsyncStatus_Success) {
            const int32_t* binData = static_cast<const int32_t*>(
                wgpuBufferGetConstMappedRange(renderer->histogramStagingBuffer, 0, 128 * sizeof(int32_t))
            );
            if (binData) {
                // Copy density bins (first 64)
                for (int i = 0; i < 64; i++) {
                    renderer->pendingHistogramBins[i] = binData[i];
                }
                // Copy velocity bins (next 64)
                for (int i = 0; i < 64; i++) {
                    renderer->pendingHistogramBins[64 + i] = binData[64 + i];
                }
                renderer->histogramBinsReadPending = true;
                renderer->minMaxMapInFlight = false;  // Reset unified flag
            }
            wgpuBufferUnmap(renderer->histogramStagingBuffer);
        } else {
            renderer->minMaxMapInFlight = false;
        }
    };
    binMapCallbackInfo.userdata1 = this;
    binMapCallbackInfo.userdata2 = nullptr;
    wgpuBufferMapAsync2(histogramStagingBuffer, WGPUMapMode_Read, 0, 128 * sizeof(int32_t), binMapCallbackInfo);
}
