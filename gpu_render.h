#ifndef WEBGPU_RENDER_H
#define WEBGPU_RENDER_H

#include <webgpu/webgpu.h>
#include <SDL2/SDL.h>
#include "irenderer.h"
#include "config.h"
#include "boilerplate.h"
#include <vector>
#include <string>
#include <fstream>

struct alignas(16) Vec4Int {
    int x, y, z, w;
};

// RENDER PASS UNIFORM
struct alignas(16) UniformData {
    int drawTarget; // 0=pressure, 1=smoke, 2=both, 3=ink
    int gridX;
    int gridY;
    float cellSize;
    float pressureMin;
    float pressureMax;
    int drawVelocities;
    float velScale;
    float windowWidth;
    float windowHeight;
    float simWidth;
    float simHeight;
    int disableHistograms; // 0=enabled, 1=disabled

    // Viewport configuration (up to 4 viewports)
    int viewportCount;
    int viewportX[4];
    int viewportY[4];
    int viewportWidth[4];
    int viewportHeight[4];
    int viewportRenderTarget[4];
    int pad1[3]; // alignment padding

    // Histogram configuration
    int densityHistogramEnabled;
    int densityHistogramX;
    int densityHistogramY;
    int densityHistogramWidth;
    int densityHistogramHeight;
    int velocityHistogramEnabled;
    int velocityHistogramX;
    int velocityHistogramY;
    int velocityHistogramWidth;
    int velocityHistogramHeight;

    // Histogram data
    float densityHistogramMin;
    float densityHistogramMax;
    int densityHistogramMaxCount;
    float velocityHistogramMin;
    float velocityHistogramMax;
    int velocityHistogramMaxCount;
    int pad0;
    Vec4Int densityHistogramBins[16];
    Vec4Int velocityHistogramBins[16];
};
static_assert(sizeof(UniformData) % 16 == 0, "UniformData invalid alignment");

class GPURenderer : public WGPUBoilerplate, public IRenderer {
public:
    static constexpr WGPUTextureUsageFlags TEXTURE_BINDING_FLAGS = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    static constexpr int HISTOGRAM_FRAME_INTERVAL = 1; // compute histograms every n frames

    GPURenderer(SDL_Window* window, const Config& config);
    ~GPURenderer();

    bool init(const Config& config) override;
    void render(const ISimulator& simulator) override;
private:
    SDL_Window* window;
    int windowWidth = 0, windowHeight = 0;

    // render state
    UniformData uniformData;
    bool initialized = false;
    bool usingGPUTextures = false;

    // histogram state (CPU/HYBRID)
    bool minMaxReadPending = false;
    int frameCount = 0;
    std::vector<int> densityHistogramBins = std::vector<int>(IRenderer::HISTOGRAM_BINS, 0);
    float densityHistogramMin = -1.0f;
    float densityHistogramMax = 1.0f;
    int densityHistogramMaxCount = 0;
    std::vector<int> velocityHistogramBins = std::vector<int>(IRenderer::HISTOGRAM_BINS, 0);
    float velocityHistogramMin = 0.0f;
    float velocityHistogramMax = 1.0f;
    int velocityHistogramMaxCount = 0;

    // webgpu core
    WGPUInstance instance = nullptr;
    WGPUSurface surface = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUTextureFormat surfaceFormat;
    WGPUBuffer uniformBuffer = nullptr;
    WGPUSampler sampler = nullptr;
    // for cpu sim
    WGPURenderPipeline renderPipeline = nullptr;
    WGPUBindGroup uniformBindGroup = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    // for gpu sim
    WGPURenderPipeline renderPipelineGPU = nullptr;
    WGPUBindGroup uniformBindGroupGPU = nullptr;
    WGPUBindGroupLayout bindGroupLayoutGPU = nullptr;

    // textures and views
    DECLARE_TEXTURE_AND_VIEW(pressure)
    DECLARE_STORAGE_VIEW(pressure)
    DECLARE_TEXTURE_AND_VIEW(density)
    DECLARE_TEXTURE_AND_VIEW(velocity)
    DECLARE_TEXTURE_AND_VIEW(solid)
    DECLARE_STORAGE_VIEW(solid)
    DECLARE_TEXTURE_AND_VIEW(redInk)
    DECLARE_TEXTURE_AND_VIEW(greenInk)
    DECLARE_TEXTURE_AND_VIEW(blueInk)

    // main render loop
    void updateUniformBufferRender(const ISimulator& simulator);
    void updateTextures(const ISimulator& simulator);
    void computeHistograms(const ISimulator& simulator);

    // gpu resource initialization helpers
    struct RenderPipelineResult {
        WGPURenderPipeline pipeline = nullptr;
        WGPUBindGroupLayout bindGroupLayout = nullptr;
    };
    bool createTexture(const TextureDesc& desc, WGPUTexture& texture, WGPUTextureView& view);
    void copyTextureHostToDevice(WGPUTexture texture, const float* data, size_t dataSize, int gridX, int gridY, int channelCount = 1);
    WGPUSurfaceConfiguration createSurfaceConfiguration();
    WGPURenderPassColorAttachment createRenderPassColorAttachment(WGPUTextureView view, WGPULoadOp loadOp, WGPUStoreOp storeOp, WGPUColor clearValue);
    WGPURenderPassDescriptor createRenderPassDescriptor(WGPURenderPassColorAttachment* colorAttachment);
    WGPUTextureViewDescriptor createTextureViewDescriptor(WGPUTextureFormat format);
    WGPUBindGroupLayout createRenderBindGroupLayout(int textureCount, WGPUShaderStage visibility, size_t uniformSize);
    RenderPipelineResult createRenderPipelineWithLayout(const char* vertexShaderFile, const char* fragmentShaderFile, const char* fragmentEntry, WGPUTextureFormat surfaceFormat, int textureCount, WGPUShaderStage visibility, size_t uniformSize);
    WGPUBindGroupLayoutEntry createSamplerLayoutEntry(int binding, WGPUShaderStage visibility);
    WGPUBindGroupEntry createSamplerBindGroupEntry(int binding, WGPUSampler sampler);

    // gpu resource instantiation boilerplate
    bool initDevice();
    bool initSurface();
    bool initRenderPipeline();
    bool initBuffers();
};

#endif
