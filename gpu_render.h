#ifndef WEBGPU_RENDER_H
#define WEBGPU_RENDER_H

#include <webgpu/webgpu.h>
#include <SDL2/SDL.h>
#include "irenderer.h"
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
    WebGPURenderer(SDL_Window* window, bool drawVelocities = false, int drawTarget = 2, bool disableHistograms = false);
    ~WebGPURenderer();

    bool init() override;
    void cleanup() override {}
    void render(const ISimulator& simulator) override;

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
    WGPURenderPipeline renderPipeline;
    WGPUBindGroup uniformBindGroup;
    WGPUBindGroupLayout bindGroupLayout;
  
    // buffers and textures
    WGPUBuffer uniformBuffer;
    WGPUTexture pressureTexture;
    WGPUTexture densityTexture;
    WGPUTexture velocityTexture;
    WGPUTexture solidTexture;
    WGPUTexture redInkTexture;
    WGPUTexture greenInkTexture;
    WGPUTexture blueInkTexture;
    WGPUTexture waterTexture;
    WGPUSampler sampler;

    // simulation data textures
    WGPUTextureView pressureTextureView;
    WGPUTextureView densityTextureView;
    WGPUTextureView velocityTextureView;
    WGPUTextureView solidTextureView;
    WGPUTextureView redInkTextureView;
    WGPUTextureView greenInkTextureView;
    WGPUTextureView blueInkTextureView;
    WGPUTextureView waterTextureView;

    // render state
    UniformData uniformData;
    bool initialized;
    bool disableHistograms;
    
    // histogram state
    int frameCount;
    std::vector<int> densityHistogramBins;
    float densityHistogramMin, densityHistogramMax;
    int densityHistogramMaxCount;
    std::vector<int> velocityHistogramBins;
    float velocityHistogramMin, velocityHistogramMax;
    int velocityHistogramMaxCount;

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
    void createRenderPass();
    void drawFrame();

    // utilities
    WGPUShaderModule loadShader(const char* source);
    std::string readFile(const char* filename);
    void releaseResources();
};

#endif