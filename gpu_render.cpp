#include "gpu_render.h"
#include <sdl2webgpu.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

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
      uniformBindGroup(nullptr),
      bindGroupLayout(nullptr),
      uniformBuffer(nullptr),
      pressureTexture(nullptr),
      densityTexture(nullptr),
      velocityTexture(nullptr),
      solidTexture(nullptr),
      redInkTexture(nullptr),
      greenInkTexture(nullptr),
      blueInkTexture(nullptr),
      waterTexture(nullptr),
      sampler(nullptr),
      pressureTextureView(nullptr),
      densityTextureView(nullptr),
      velocityTextureView(nullptr),
      solidTextureView(nullptr),
      redInkTextureView(nullptr),
      greenInkTextureView(nullptr),
      blueInkTextureView(nullptr),
      waterTextureView(nullptr),
      initialized(false),
      drawTarget(config.rendering.target),
      showVelocityVectors(config.rendering.showVelocityVectors),
      disableHistograms(config.rendering.disableHistograms),
      velocityScale(config.rendering.velocityScale),
      frameCount(0),
      densityHistogramBins(IRenderer::HISTOGRAM_BINS, 0),
      densityHistogramMin(0.0f),
      densityHistogramMax(0.0f),
      densityHistogramMaxCount(0),
      velocityHistogramBins(IRenderer::HISTOGRAM_BINS, 0),
      velocityHistogramMin(0.0f),
      velocityHistogramMax(0.0f),
      velocityHistogramMaxCount(0)
{

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
    if (waterTextureView) {
        wgpuTextureViewRelease(waterTextureView);
        waterTextureView = nullptr;
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
    if (waterTexture) {
        wgpuTextureRelease(waterTexture);
        waterTexture = nullptr;
    }
    
    // other resources
    if (renderPipeline) {
        wgpuRenderPipelineRelease(renderPipeline);
        renderPipeline = nullptr;
    }
    if (uniformBindGroup) {
        wgpuBindGroupRelease(uniformBindGroup);
        uniformBindGroup = nullptr;
    }
    if (bindGroupLayout) {
        wgpuBindGroupLayoutRelease(bindGroupLayout);
        bindGroupLayout = nullptr;
    }
    if (sampler) {
        wgpuSamplerRelease(sampler);
        sampler = nullptr;
    }
    if (uniformBuffer) {
        wgpuBufferRelease(uniformBuffer);
        uniformBuffer = nullptr;
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
        // water texture
        {
            .binding = 9,
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

    return true;
}

void WebGPURenderer::computeHistograms(const ISimulator& simulator) {
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

void WebGPURenderer::updateUniformData(const ISimulator& simulator) {
    uniformData.gridX = simulator.getGridX();
    uniformData.gridY = simulator.getGridY();
    uniformData.cellSize = simulator.getCellSize();
    uniformData.simWidth = uniformData.gridX * uniformData.cellSize;
    uniformData.simHeight = uniformData.gridY * uniformData.cellSize;

    // pressure range
    const auto& pressure = simulator.getPressure();
    if (!pressure.empty()) {
        uniformData.pressureMin = *std::min_element(pressure.begin(), pressure.end());
        uniformData.pressureMax = *std::max_element(pressure.begin(), pressure.end());
    }

    // histogram data
    uniformData.densityHistogramMin = densityHistogramMin;
    uniformData.densityHistogramMax = densityHistogramMax;
    uniformData.velocityHistogramMin = velocityHistogramMin;
    uniformData.velocityHistogramMax = velocityHistogramMax;
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

    // create textures initially or on resize
    if (!pressureTexture || uniformData.gridX != gridX || uniformData.gridY != gridY) {
        // release old textures (views first, then textures)
        if (pressureTextureView) {
            wgpuTextureViewRelease(pressureTextureView);
            pressureTextureView = nullptr;
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
        if (waterTextureView) {
            wgpuTextureViewRelease(waterTextureView);
            waterTextureView = nullptr;
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
        if (waterTexture) {
            wgpuTextureRelease(waterTexture);
            waterTexture = nullptr;
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

        textureDesc.label = "Water Texture";
        waterTexture = wgpuDeviceCreateTexture(device, &textureDesc);

        if (!pressureTexture || !densityTexture || !velocityTexture || !solidTexture ||
            !redInkTexture || !greenInkTexture || !blueInkTexture || !waterTexture) {
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
        waterTextureView = wgpuTextureCreateView(waterTexture, &viewDesc);

        if (!pressureTextureView || !densityTextureView || !velocityTextureView || !solidTextureView ||
            !redInkTextureView || !greenInkTextureView || !blueInkTextureView || !waterTextureView) {
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
            },
            {
                .binding = 9,
                .textureView = waterTextureView
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

        // write water data to texture
        const auto& water = simulator.getWaterContent();
        if (!water.empty()) {
            WGPUImageCopyTexture waterCopy = {
                .texture = waterTexture,
                .mipLevel = 0,
                .origin = {0, 0, 0},
                .aspect = WGPUTextureAspect_All
            };

            WGPUTextureDataLayout waterLayout = {
                .offset = 0,
                .bytesPerRow = static_cast<uint32_t>(gridX * sizeof(float)),
                .rowsPerImage = static_cast<uint32_t>(gridY)
            };

            WGPUExtent3D waterExtent = {
                .width = static_cast<uint32_t>(gridX),
                .height = static_cast<uint32_t>(gridY),
                .depthOrArrayLayers = 1
            };

            wgpuQueueWriteTexture(queue, &waterCopy, water.data(),
                                   water.size() * sizeof(float), &waterLayout, &waterExtent);
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

    // set pipeline and bind groups
    wgpuRenderPassEncoderSetPipeline(renderPassEncoder, renderPipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPassEncoder, 0, uniformBindGroup, 0, nullptr);

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
}
