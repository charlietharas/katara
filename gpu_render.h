#ifndef WEBGPU_RENDER_H
#define WEBGPU_RENDER_H

#include <webgpu/webgpu.h>
#include <SDL2/SDL.h>
#include "irenderer.h"
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

class WebGPURenderer : public IRenderer {
public:
    WebGPURenderer(SDL_Window* window);
    ~WebGPURenderer();

    bool init() override;
    void cleanup() override;
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
    void updateUniformData(const ISimulator& simulator);
    void updateSimulationTextures(const ISimulator& simulator);
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