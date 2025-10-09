#include "gpu_render.h"
#include <sdl2webgpu.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

// vertex shader
const char* WebGPURenderer::vertexShaderSource = R"(
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> @builtin(position) vec4<f32> {
    var positions = array<vec2<f32>, 6>(
        vec2<f32>(-1.0, -1.0), // bottom left
        vec2<f32>( 1.0, -1.0), // bottom right
        vec2<f32>(-1.0,  1.0), // top left
        vec2<f32>( 1.0, -1.0), // bottom right
        vec2<f32>( 1.0,  1.0), // top right
        vec2<f32>(-1.0,  1.0)  // top left
    );
    return vec4<f32>(positions[vertexIndex], 0.0, 1.0);
}
)";

// fragment shader
const char* WebGPURenderer::fragmentShaderSource = R"(
struct UniformData {
    drawTarget: i32,
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    pressureMin: f32,
    pressureMax: f32,
    drawVelocities: i32,
    velScale: f32,
    windowWidth: f32,
    windowHeight: f32,
    simWidth: f32,
    simHeight: f32,
    padding: vec2<f32>, // 16-byte alignment
};

@group(0) @binding(0) var<uniform> uniforms: UniformData;
@group(0) @binding(1) var pressureSampler: sampler;
@group(0) @binding(2) var pressureTexture: texture_2d<f32>;
@group(0) @binding(3) var densityTexture: texture_2d<f32>;
@group(0) @binding(4) var velocityTexture: texture_2d<f32>;
@group(0) @binding(5) var solidTexture: texture_2d<f32>;

fn mapValueToColor(value: f32, min: f32, max: f32) -> vec3<f32> {
    var clampedValue = clamp(value, min, max - 0.0001);
    var delta = max - min;
    var normalized = select(0.5, (clampedValue - min) / delta, delta != 0.0);

    var m = 0.25;
    var num = i32(normalized / m);
    var s = (normalized - f32(num) * m) / m;

    var color = vec3<f32>(0.0, 0.0, 0.0);

    switch(num) {
        case 0: { color = vec3<f32>(0.0, s, 1.0); break; }
        case 1: { color = vec3<f32>(0.0, 1.0, 1.0 - s); break; }
        case 2: { color = vec3<f32>(s, 1.0, 0.0); break; }
        case 3: { color = vec3<f32>(1.0, 1.0 - s, 0.0); break; }
        default: { color = vec3<f32>(1.0, 0.0, 0.0); break; }
    }

    return color;
}

fn mapValueToGreyscale(value: f32, min: f32, max: f32) -> vec3<f32> {
    var t = (value - min) / (max - min);
    t = clamp(t, 0.0, 1.0);
    return vec3<f32>(t, t, t);
}

fn worldToScreen(worldPos: vec2<f32>) -> vec2<f32> {
    var screenPos = worldPos;
    screenPos.y = uniforms.simHeight - worldPos.y;
    screenPos = screenPos / vec2<f32>(uniforms.simWidth, uniforms.simHeight);
    screenPos = screenPos * 2.0 - 1.0;
    return screenPos;
}

fn sampleFluidField(coord: vec2<f32>) -> vec4<f32> {
    var gridCoord = coord / uniforms.cellSize;

    // integer grid coordinates
    var gridX = i32(gridCoord.x);
    var gridY = i32(gridCoord.y);

    // bounds
    if (gridX < 0 || gridX >= uniforms.gridX ||
        gridY < 0 || gridY >= uniforms.gridY) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }

    var texX = gridX; // col index
    var texY = gridY; // row index

    // load simulation data
    var pressure = textureLoad(pressureTexture, vec2<i32>(texX, texY), 0);
    var density = textureLoad(densityTexture, vec2<i32>(texX, texY), 0);
    var solid = textureLoad(solidTexture, vec2<i32>(texX, texY), 0);

    var color = vec3<f32>(0.0, 0.0, 0.0);

    if (solid.r > 0.5) {
        // fluid cell
        if (uniforms.drawTarget == 0) {
            // draw pressure
            color = mapValueToColor(pressure.r, uniforms.pressureMin, uniforms.pressureMax);
        } else if (uniforms.drawTarget == 1) {
            // draw smoke/density
            color = mapValueToGreyscale(density.r, 0.0, 1.0);
        } else {
            // draw pretty pressure + smoke
            color = mapValueToColor(pressure.r, uniforms.pressureMin, uniforms.pressureMax);
            color = color - density.r * vec3<f32>(1.0, 1.0, 1.0);
            color = max(color, vec3<f32>(0.0, 0.0, 0.0));
        }
    } else {
        // solid boundary
        color = vec3<f32>(0.49, 0.49, 0.49); // gray
    }

    return vec4<f32>(color, 1.0);
}

fn drawVelocityField(coord: vec2<f32>) -> vec4<f32> {
    var gridCoord = coord / uniforms.cellSize;

    // integer grid coordinates
    var gridX = i32(gridCoord.x);
    var gridY = i32(gridCoord.y);

    // bounds
    if (gridX < 0 || gridX >= uniforms.gridX ||
        gridY < 0 || gridY >= uniforms.gridY) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }

    var texX = gridX; // col index
    var texY = gridY; // row index

    // load simulation data
    var solid = textureLoad(solidTexture, vec2<i32>(texX, texY), 0);
    var velocity = textureLoad(velocityTexture, vec2<i32>(texX, texY), 0);

    // only show velocity in fluid cells
    if (solid.r <= 0.5) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    // draw lines for velocity
    var velMag = length(velocity.xy);
    if (velMag > 0.001) {
        return vec4<f32>(1.0, 1.0, 1.0, velMag * uniforms.velScale);
    }

    return vec4<f32>(0.0, 0.0, 0.0, 0.0);
}

@fragment
fn fs_main(@builtin(position) fragCoord: vec4<f32>) -> @location(0) vec4<f32> {
    var pixelCoord = fragCoord.xy;

    // pixel -> world coords
    var worldCoord = vec2<f32>(
        pixelCoord.x / uniforms.windowWidth * uniforms.simWidth,
        (uniforms.windowHeight - pixelCoord.y) / uniforms.windowHeight * uniforms.simHeight
    );

    var color = sampleFluidField(worldCoord);

    if (uniforms.drawVelocities != 0) {
        var velColor = drawVelocityField(worldCoord);
        // blend velocity lines
        color = mix(color, velColor, velColor.a * 0.5);
    }

    return color;
}
)";

WebGPURenderer::WebGPURenderer(SDL_Window* window)
    : window(window), windowWidth(0), windowHeight(0),
      instance(nullptr), surface(nullptr), adapter(nullptr), device(nullptr),
      queue(nullptr), renderPipeline(nullptr),
      uniformBindGroup(nullptr), bindGroupLayout(nullptr),
      uniformBuffer(nullptr), vertexBuffer(nullptr),
      pressureTexture(nullptr), densityTexture(nullptr), velocityTexture(nullptr), solidTexture(nullptr),
      sampler(nullptr),
      pressureTextureView(nullptr), densityTextureView(nullptr), velocityTextureView(nullptr), solidTextureView(nullptr),
      initialized(false) {

    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    uniformData = {};
    uniformData.drawTarget = 2; // draw pressure and smoke
    uniformData.drawVelocities = 0;
    uniformData.velScale = 0.05f;
    uniformData.windowWidth = static_cast<float>(windowWidth);
    uniformData.windowHeight = static_cast<float>(windowHeight);
}

WebGPURenderer::~WebGPURenderer() {
    cleanup();
}

bool WebGPURenderer::init() {
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

    if (!initBindGroups()) {
        std::cerr << "Failed to initialize bind groups" << std::endl;
        return false;
    }

    initialized = true;
    return true;
}

void WebGPURenderer::cleanup() {
    if (!initialized) return;

    releaseResources();
    initialized = false;
}

void WebGPURenderer::releaseResources() {
    if (renderPipeline) wgpuRenderPipelineRelease(renderPipeline);
    if (uniformBindGroup) wgpuBindGroupRelease(uniformBindGroup);
    if (bindGroupLayout) wgpuBindGroupLayoutRelease(bindGroupLayout);
    if (pressureTextureView) wgpuTextureViewRelease(pressureTextureView);
    if (densityTextureView) wgpuTextureViewRelease(densityTextureView);
    if (velocityTextureView) wgpuTextureViewRelease(velocityTextureView);
    if (solidTextureView) wgpuTextureViewRelease(solidTextureView);
    if (pressureTexture) wgpuTextureRelease(pressureTexture);
    if (densityTexture) wgpuTextureRelease(densityTexture);
    if (velocityTexture) wgpuTextureRelease(velocityTexture);
    if (solidTexture) wgpuTextureRelease(solidTexture);
    if (sampler) wgpuSamplerRelease(sampler);
    if (vertexBuffer) wgpuBufferRelease(vertexBuffer);
    if (uniformBuffer) wgpuBufferRelease(uniformBuffer);
    if (queue) wgpuQueueRelease(queue);
    if (device) wgpuDeviceRelease(device);
    if (adapter) wgpuAdapterRelease(adapter);
    if (surface) wgpuSurfaceRelease(surface);
    if (instance) wgpuInstanceRelease(instance);
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

    // vertex buffer
    WGPUBufferDescriptor vertexBufferDesc = {};
    vertexBufferDesc.nextInChain = nullptr;
    vertexBufferDesc.label = "Vertex Buffer";
    vertexBufferDesc.size = 6 * sizeof(float) * 2; // 6 vertices * 2 floats per vertex
    vertexBufferDesc.usage = WGPUBufferUsage_Vertex;
    vertexBufferDesc.mappedAtCreation = false;

    vertexBuffer = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);
    if (!vertexBuffer) {
        std::cerr << "Failed to create vertex buffer" << std::endl;
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
    WGPUShaderModule vertexShader = loadShader(vertexShaderSource);
    WGPUShaderModule fragmentShader = loadShader(fragmentShaderSource);

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

bool WebGPURenderer::initBindGroups() {
    // bind groups created in updateSimulationTextures
    // (need to have the texture views ready)

    return true;
}

void WebGPURenderer::updateUniformData(const FluidSimulator& simulator) {
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

    // update uniform buffer
    wgpuQueueWriteBuffer(queue, uniformBuffer, 0, &uniformData, sizeof(UniformData));
}

void WebGPURenderer::updateSimulationTextures(const FluidSimulator& simulator) {
    int gridX = simulator.getGridX();
    int gridY = simulator.getGridY();

    // create textures initially or on resize
    if (!pressureTexture || uniformData.gridX != gridX || uniformData.gridY != gridY) {
        // release old textures
        if (pressureTextureView) wgpuTextureViewRelease(pressureTextureView);
        if (densityTextureView) wgpuTextureViewRelease(densityTextureView);
        if (velocityTextureView) wgpuTextureViewRelease(velocityTextureView);
        if (solidTextureView) wgpuTextureViewRelease(solidTextureView);
        if (pressureTexture) wgpuTextureRelease(pressureTexture);
        if (densityTexture) wgpuTextureRelease(densityTexture);
        if (velocityTexture) wgpuTextureRelease(velocityTexture);
        if (solidTexture) wgpuTextureRelease(solidTexture);

        // create new textures
        WGPUTextureDescriptor textureDesc = {};
        textureDesc.nextInChain = nullptr;
        textureDesc.size = { static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1 };
        textureDesc.mipLevelCount = 1;
        textureDesc.sampleCount = 1;
        textureDesc.dimension = WGPUTextureDimension_2D;
        textureDesc.format = WGPUTextureFormat_R32Float;
        textureDesc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;

        pressureTexture = wgpuDeviceCreateTexture(device, &textureDesc);
        textureDesc.label = "Pressure Texture";
        pressureTexture = wgpuDeviceCreateTexture(device, &textureDesc);

        textureDesc.label = "Density Texture";
        densityTexture = wgpuDeviceCreateTexture(device, &textureDesc);

        textureDesc.label = "Velocity Texture";
        textureDesc.format = WGPUTextureFormat_RG32Float; // R=X velocity, G=Y velocity
        velocityTexture = wgpuDeviceCreateTexture(device, &textureDesc);

        textureDesc.label = "Solid Texture";
        textureDesc.format = WGPUTextureFormat_R32Float; // Single channel for solid/fluid
        solidTexture = wgpuDeviceCreateTexture(device, &textureDesc);

        if (!pressureTexture || !densityTexture || !velocityTexture || !solidTexture) {
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

        if (!pressureTextureView || !densityTextureView || !velocityTextureView || !solidTextureView) {
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
    }
}

void WebGPURenderer::render(const FluidSimulator& simulator) {
    if (!initialized) return;

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

