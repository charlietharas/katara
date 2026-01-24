#ifndef WEBGPU_RENDER_H
#define WEBGPU_RENDER_H

#include <webgpu/webgpu.h>
#include <SDL2/SDL.h>
#include "irenderer.h"
#include "config.h"
#include <vector>
#include <string>
#include <fstream>

struct alignas(16) Vec4Int {
    int x, y, z, w;
};

struct UniformData {
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
    float densityHistogramMin;
    float densityHistogramMax;
    float velocityHistogramMin;
    float velocityHistogramMax;
    int densityHistogramMaxCount;
    int velocityHistogramMaxCount;
    Vec4Int densityHistogramBins[16]; // packed as vec4 for 16-byte alignment
    Vec4Int velocityHistogramBins[16]; // packed as vec4 for 16-byte alignment
};

class WebGPURenderer : public IRenderer {
public:
    WebGPURenderer(SDL_Window* window, const Config& config);
    ~WebGPURenderer();

    bool init(const Config& config) override;
    void cleanup() override {}
    void render(const ISimulator& simulator) override;

    WGPUDevice getDevice() const { return device; }
    WGPUQueue getQueue() const { return queue; }
    bool isInitialized() const { return initialized; }

private:
    SDL_Window* window;
    int windowWidth, windowHeight;

    // WebGPU objects
    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat surfaceFormat;
    // for cpu sim
    WGPURenderPipeline renderPipeline;
    WGPUBindGroup uniformBindGroup;
    WGPUBindGroupLayout bindGroupLayout;
    // for gpu sim
    WGPURenderPipeline renderPipelineGPU;
    WGPUBindGroup uniformBindGroupGPU;
    WGPUBindGroupLayout bindGroupLayoutGPU;
    // for pressure min/max compute
    WGPUComputePipeline computePipelineMinMax;
    WGPUBindGroup bindGroupMinMax;
    WGPUBindGroupLayout bindGroupLayoutMinMax;
  
    // buffers and textures
    WGPUBuffer uniformBuffer;
    WGPUTexture pressureTexture;
    WGPUTexture densityTexture;
    WGPUTexture velocityTexture;
    WGPUTexture solidTexture;
    WGPUTexture redInkTexture;
    WGPUTexture greenInkTexture;
    WGPUTexture blueInkTexture;
    WGPUSampler sampler;

    // simulation data textures
    WGPUTextureView pressureTextureView;
    WGPUTextureView pressureTextureStorageView;
    WGPUTextureView densityTextureView;
    WGPUTextureView velocityTextureView;
    WGPUTextureView solidTextureView;
    WGPUTextureView solidTextureStorageView;
    WGPUTextureView redInkTextureView;
    WGPUTextureView greenInkTextureView;
    WGPUTextureView blueInkTextureView;

    // unified min/max buffers (pressure + velocity, 4 floats total)
    WGPUBuffer minMaxBuffer;
    WGPUBuffer minMaxStagingBuffer;

    // histogram bin buffer
    WGPUBuffer histogramBinBuffer;            // 128 ints: 64 density + 64 velocity bins
    WGPUBuffer histogramStagingBuffer;        // Staging for histogram bin readback

    // histogram compute pipeline (for bin counting only)
    WGPUComputePipeline computePipelineHistogramBins;
    WGPUBindGroupLayout bindGroupLayoutHistogramBins;
    WGPUBindGroup bindGroupHistogramBins;

    // small uniform buffer for passing min/max to histogram bin shader
    WGPUBuffer minMaxUniformBuffer;

    // render state
    UniformData uniformData;
    bool initialized;
    bool usingGPUTextures;

    // cached config values
    int drawTarget;
    bool showVelocityVectors;
    bool disableHistograms;
    float velocityScale;

    // histogram state
    int frameCount;
    std::vector<int> densityHistogramBins;
    float densityHistogramMin, densityHistogramMax;
    int densityHistogramMaxCount;
    std::vector<int> velocityHistogramBins;
    float velocityHistogramMin, velocityHistogramMax;
    int velocityHistogramMaxCount;

    // unified min/max readback state
    float pendingPressureMinMax[2];  // pressMin, pressMax
    float pendingVelocityMinMax[2];  // velMin, velMax
    bool minMaxReadPending;
    bool minMaxMapInFlight;

    // histogram readback state
    int pendingHistogramBins[128];            // 64 density + 64 velocity bins
    bool histogramBinsReadPending;
    bool histogramMapInFlight;

    // initialization methods
    bool initWebGPU();
    bool initDevice();
    bool initSurface();
    bool initRenderPipeline();
    bool initBuffers();
    bool initTextures();

    // render methods
    void updateUniformData(const ISimulator& simulator);
    void updateSimulationTextures(const ISimulator& simulator);
    void computeHistograms(const ISimulator& simulator);
    void dispatchHistogramBinCounting();
    void createRenderPass();
    void drawFrame();

    // utilities
    WGPUShaderModule loadShader(const char* source);
    void releaseResources();
};

#endif