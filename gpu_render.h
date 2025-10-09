#ifndef WEBGPU_RENDER_H
#define WEBGPU_RENDER_H

#include <webgpu/webgpu.h>
#include <SDL2/SDL.h>
#include "sim.h"
#include <vector>

struct UniformData {
    int drawTarget; // 0=pressure, 1=smoke, 2=both
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
    float padding[2]; // 16-byte alignment
};

class WebGPURenderer {
public:
    WebGPURenderer(SDL_Window* window);
    ~WebGPURenderer();

    bool init();
    void cleanup();
    void render(const FluidSimulator& simulator);

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
    WGPUBuffer vertexBuffer;
    WGPUTexture pressureTexture;
    WGPUTexture densityTexture;
    WGPUTexture velocityTexture;
    WGPUTexture solidTexture;
    WGPUSampler sampler;

    // simulation data textures
    WGPUTextureView pressureTextureView;
    WGPUTextureView densityTextureView;
    WGPUTextureView velocityTextureView;
    WGPUTextureView solidTextureView;

    // render state
    UniformData uniformData;
    bool initialized;

    // initialization methods
    bool initWebGPU();
    bool initDevice();
    bool initSurface();
    bool initRenderPipeline();
    bool initBuffers();
    bool initTextures();
    bool initBindGroups();

    // render methods
    void updateUniformData(const FluidSimulator& simulator);
    void updateSimulationTextures(const FluidSimulator& simulator);
    void createRenderPass();
    void drawFrame();

    // utilz
    WGPUShaderModule loadShader(const char* filename);
      void releaseResources();

    // shader source strings
    static const char* vertexShaderSource;
    static const char* fragmentShaderSource;
};

#endif