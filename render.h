#ifndef RENDER_H
#define RENDER_H

#include <webgpu/webgpu.h>
#include <SDL2/SDL.h>
#include "render_histogram.h"
#include "sim_shared.h"
#include "config.h"
#include "boilerplate.h"
#include "shader_bundle.h"
#include <array>
#include <optional>
#include <vector>
#include <string>
#include <fstream>

struct alignas(16) Vec4Int {
    int x, y, z, w;
};

struct alignas(16) Vec4Float {
    float x, y, z, w;
};

// RENDER PASS UNIFORM
struct alignas(16) UniformData {
    int gridX;
    int gridY;
    float cellSize;
    float pressureMin;
    float pressureMax;
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
    int viewportRenderVelocity[4];
    int viewportRotation[4];

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
    int entropyTimeSeriesEnabled;
    int entropyTimeSeriesX;
    int entropyTimeSeriesY;
    int entropyTimeSeriesWidth;
    int entropyTimeSeriesHeight;
    int entropyHistoryCount;
    int entropyHistoryWriteIndex;
    int entropyPad0;
    Vec4Float entropyHistory[16];

    // Volume time series configuration + data (2 lines)
    int volumeTimeSeriesEnabled;
    int volumeTimeSeriesX;
    int volumeTimeSeriesY;
    int volumeTimeSeriesWidth;
    int volumeTimeSeriesHeight;
    float volumeHistoryMax;
    int volumeHistoryCount;
    int volumeHistoryWriteIndex;
    int volumePad0;
    Vec4Float volumeDomainHistory[16];
    Vec4Float volumeMassHistory[16];

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
    alignas(16) Vec4Float backgroundColor;  // .rgb = background color, .a unused
};
static_assert(sizeof(UniformData) % 16 == 0, "UniformData invalid alignment");

class Renderer : public WGPUBoilerplate {
public:
    static constexpr WGPUTextureUsageFlags TEXTURE_BINDING_FLAGS = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    static constexpr int HISTOGRAM_FRAME_INTERVAL = 1; // compute histograms every n frames
    static constexpr float ENTROPY_EPSILON = 1e-6f;
    static constexpr int ENTROPY_HISTORY_SAMPLES = 64;
    static constexpr int VOLUME_HISTORY_SAMPLES = 64;
    static constexpr float VOLUME_DENSITY_SCALE = 1024.0f;

    Renderer(SDL_Window* window, const Config& config);
    ~Renderer();

    bool init(const Config& config);
    void render(const ISimulator& simulator);
    void resetEntropyTimeSeries();

    bool setViewSource(int index, const std::string& wgsl);
    bool resetViewSource(int index);
    bool applyViewShaders();
    std::string getViewSource(int index) const;
    std::string getLastShaderError() const;
    void loadPersistedViewOverrides();
    void setBackgroundColor(float r, float g, float b);
    void onWindowResize(int width, int height);

private:
    SDL_Window* window;
    int windowWidth = 0, windowHeight = 0;

    // render state
    UniformData uniformData;
    bool initialized = false;
    bool usingGPUTextures = false;

    // runtime-modifiable background color (default: Palette.black = '#050505')
    struct {
        float r = 5.0f / 255.0f;
        float g = 5.0f / 255.0f;
        float b = 5.0f / 255.0f;
    } backgroundColor;

    // histogram state (CPU/HYBRID)
    bool minMaxReadPending = false;
    int frameCount = 0;
    std::vector<int> densityHistogramBins = std::vector<int>(RenderHistogram::HISTOGRAM_BINS, 0);
    float densityHistogramMin = -1.0f;
    float densityHistogramMax = 1.0f;
    int densityHistogramMaxCount = 0;
    std::vector<int> velocityHistogramBins = std::vector<int>(RenderHistogram::HISTOGRAM_BINS, 0);
    float velocityHistogramMin = 0.0f;
    float velocityHistogramMax = 1.0f;
    int velocityHistogramMaxCount = 0;
    float entropyNormalized = 0.0f;
    std::array<float, ENTROPY_HISTORY_SAMPLES> entropyHistory = {};
    int entropyHistoryWriteIndex = 0;
    int entropyHistoryCount = 0;

    std::array<float, VOLUME_HISTORY_SAMPLES> volumeDomainHistory = {};
    std::array<float, VOLUME_HISTORY_SAMPLES> volumeMassHistory = {};
    int volumeHistoryWriteIndex = 0;
    int volumeHistoryCount = 0;
    float volumeHistoryMax = 1.0f;

    // webgpu core
    WGPUInstance instance = nullptr;
    WGPUSurface surface = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUTextureFormat surfaceFormat;
    WGPUBuffer uniformBuffer = nullptr;
    WGPUSampler sampler = nullptr;
    WGPURenderPipeline renderPipeline = nullptr;
    WGPUBindGroup uniformBindGroup = nullptr;
    WGPUBindGroup uniformBindGroupGPU = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;

    std::vector<ViewRegistryEntry> builtinViews;
    std::array<std::optional<std::string>, kViewTargetCount> viewOverrides = {};
    std::string lastShaderError;

    // CPU texture upload dimensions (decoupled from uniformData, which is updated before upload)
    int textureGridX = 0;
    int textureGridY = 0;

    // textures and views
    DECLARE_TEXTURE_AND_VIEW(pressure)
    DECLARE_STORAGE_VIEW(pressure)
    DECLARE_TEXTURE_AND_VIEW(density)
    DECLARE_TEXTURE_AND_VIEW(velocity)
    DECLARE_TEXTURE_AND_VIEW(solid)
    DECLARE_STORAGE_VIEW(solid)
    DECLARE_TEXTURE_AND_VIEW(ink)
    WGPUTexture gpuPressureTextureRef = nullptr;
    WGPUTexture gpuDensityTextureRef = nullptr;
    WGPUTexture gpuVelocityTextureRef = nullptr;
    WGPUTexture gpuSolidTextureRef = nullptr;
    WGPUTexture gpuInkTextureRef = nullptr;

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
    RenderPipelineResult createRenderPipelineWithFragmentSource(const char* vertexShaderFile, const std::string& fragmentSource, const char* fragmentEntry, WGPUTextureFormat surfaceFormat, int textureCount, WGPUShaderStage visibility, size_t uniformSize);
    std::vector<ViewRegistryEntry> buildActiveViewRegistry() const;
    bool rebuildFragmentPipelineFromActiveViews();
    WGPUBindGroupLayoutEntry createSamplerLayoutEntry(int binding, WGPUShaderStage visibility);
    WGPUBindGroupEntry createSamplerBindGroupEntry(int binding, WGPUSampler sampler);

    // gpu resource instantiation boilerplate
    bool initDevice();
    bool initSurface();
    bool initRenderPipeline();
    bool initBuffers();
};

#endif
