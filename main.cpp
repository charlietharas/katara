#define SDL_MAIN_HANDLED

#include <webgpu/webgpu.h>
#include <iostream>
#include <sdl2webgpu.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <memory>
#include "sim.h"
#include "render.h"
#include "gpu_render.h"
#include "gpu_sim.h"
#include "irenderer.h"
#include "isimulator.h"
#include "config.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cmath>
#endif

// cpu -> cpu renderer, otherwise hybrid/gpu both use gpu
std::unique_ptr<IRenderer> createRenderer(SDL_Window* window, const Config& config) {
    if (config.pipeline == PipelineType::CPU) {
        return std::make_unique<Renderer>(window, config);
    }
    return std::make_unique<GPURenderer>(window, config);
}

// gpu -> gpu simulator, otherwise hybrid/cpu both use cpu
std::unique_ptr<ISimulator> createSimulator(const Config& config) {
    if (config.pipeline == PipelineType::GPU) {
        return std::make_unique<GPUSimulator>(config);
    }
    return std::make_unique<Simulator>(config);
}

#ifdef __EMSCRIPTEN__
struct MainLoopState {
    ISimulator* simulator;
    IRenderer* renderer;
    SDL_Window* window;
    bool* running;
    int windowWidth;
    int windowHeight;
};

void mainLoopCallback(void* arg) {
    MainLoopState* s = static_cast<MainLoopState*>(arg);
    if (!*(s->running)) {
        emscripten_cancel_main_loop();
        return;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) *(s->running) = false;
        else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            auto coords = s->simulator->screenToGridCoords(event.button.x, event.button.y, s->windowWidth, s->windowHeight);
            if (s->simulator->isInsideCircle(coords.first, coords.second))
                s->simulator->onMouseDown(coords.first, coords.second);
        }
        else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
            s->simulator->onMouseUp();
        else if (event.type == SDL_MOUSEMOTION && event.motion.state & SDL_BUTTON_LMASK) {
            auto coords = s->simulator->screenToGridCoords(event.motion.x, event.motion.y, s->windowWidth, s->windowHeight);
            s->simulator->onMouseDrag(coords.first, coords.second);
        }
    }

    s->simulator->update();
    s->renderer->render(*s->simulator);
}

struct HandTrackingState {
    ISimulator* simulator = nullptr;
    int windowWidth = 0;
    int windowHeight = 0;
    bool isTracking = false;
    bool wasInside = false;
};

static HandTrackingState g_handState;

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void initHandTracking(int w, int h) {
        g_handState.windowWidth = w;
        g_handState.windowHeight = h;
    }

    EMSCRIPTEN_KEEPALIVE
    void setSimulatorPointer(void* ptr) {
        g_handState.simulator = static_cast<ISimulator*>(ptr);
    }

    EMSCRIPTEN_KEEPALIVE
    void updateHandPosition(float nx, float ny, bool present) {
        if (!g_handState.simulator || !present) {
            if (g_handState.isTracking) g_handState.simulator->onMouseUp();
            g_handState.isTracking = false;
            g_handState.wasInside = false;
            return;
        }

        // Convert normalized coords to screen (flip Y for MediaPipe)
        int sx = static_cast<int>(nx * g_handState.windowWidth);
        int sy = static_cast<int>((1.0f - ny) * g_handState.windowHeight);
        auto coords = g_handState.simulator->screenToGridCoords(sx, sy, g_handState.windowWidth, g_handState.windowHeight);
        bool inside = g_handState.simulator->isInsideCircle(coords.first, coords.second);

        if (inside) {
            if (!g_handState.isTracking && !g_handState.wasInside) {
                g_handState.simulator->onMouseDown(coords.first, coords.second);
                g_handState.isTracking = true;
            } else if (g_handState.isTracking) {
                g_handState.simulator->onMouseDrag(coords.first, coords.second);
            }
            g_handState.wasInside = true;
        } else {
            if (g_handState.isTracking) {
                g_handState.simulator->onMouseUp();
                g_handState.isTracking = false;
            }
            g_handState.wasInside = false;
        }
    }
}
#endif

int main(int argc, char** argv) {
    // load config from default path if none specified
#ifdef __EMSCRIPTEN__
    std::string configPath = "/config.json";  // Emscripten virtual FS root
#else
    std::string configPath = "../config.json";  // Native build
#endif
    if (argc > 1) {
        configPath = argv[1];
    }
    Config config = ConfigLoader::loadConfig(configPath);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "ERR initializing SDL: " << SDL_GetError() << std::endl;
        return 1;
    }

    // window size
    int windowWidth = config.window.defaultWidth;
    int windowHeight = config.window.defaultHeight;
    float aspectRatio = static_cast<float>(config.window.defaultWidth) / config.window.defaultHeight;

    // image loading
    SDL_Surface* imageSurface = nullptr;
    SDL_Surface* convertedSurface = nullptr;
    ImageData* imageData = nullptr;
    if (!config.ink.imagePath.empty() && config.rendering.target == 3) {
        // load an image and grab its dimensions for window sizing
        imageSurface = IMG_Load(config.ink.imagePath.c_str());
        if (imageSurface) {
            aspectRatio = static_cast<float>(imageSurface->w) / imageSurface->h;

            // baseSize is the smaller dimension
            if (aspectRatio >= 1.0f) { // landscape
                windowHeight = config.window.baseSize;
                windowWidth = static_cast<int>(config.window.baseSize * aspectRatio);
            } else { // portrait
                windowWidth = config.window.baseSize;
                windowHeight = static_cast<int>(config.window.baseSize / aspectRatio);
            }

            std::cout << "Got image with aspect ratio: " << aspectRatio << std::endl;
            std::cout << "Resulting window size: " << windowWidth << "x" << windowHeight << std::endl;

            // 32-bit RGB
            convertedSurface = SDL_ConvertSurfaceFormat(imageSurface, SDL_PIXELFORMAT_RGB888, 0);
            if (!convertedSurface) {
                std::cerr << "ERR converting image surface: " << SDL_GetError() << std::endl;
                SDL_FreeSurface(imageSurface);
                SDL_Quit();
                return 1;
            }

            // copy data to struct
            imageData = new ImageData();
            imageData->pixels = convertedSurface->pixels;
            imageData->width = convertedSurface->w;
            imageData->height = convertedSurface->h;
            imageData->bytesPerPixel = convertedSurface->format->BytesPerPixel;
            imageData->rShift = convertedSurface->format->Rshift;
            imageData->gShift = convertedSurface->format->Gshift;
            imageData->bShift = convertedSurface->format->Bshift;
        } else {
            std::cerr << "ERR loading image (path: " << config.ink.imagePath << "): " << IMG_GetError() << std::endl;
            SDL_Quit();
            return 1;
        }
    } else if (config.rendering.target == 3) {
        std::cerr << "ERR rendering target=3 but no image path was provided" << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("katara",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          windowWidth,
                                          windowHeight,
                                          SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "ERR creating window: " << SDL_GetError() << std::endl;
        delete imageData;
        if (convertedSurface) SDL_FreeSurface(convertedSurface);
        if (imageSurface) SDL_FreeSurface(imageSurface);
        SDL_Quit();
        return 1;
    }

    auto renderer = createRenderer(window, config);
    auto simulator = createSimulator(config);

    if (!renderer->init(config)) {
        std::cerr << "ERR initializing renderer" << std::endl;
        delete imageData;
        if (convertedSurface) SDL_FreeSurface(convertedSurface);
        if (imageSurface) SDL_FreeSurface(imageSurface);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::cout << "Renderer initialized" << std::endl;

    if (config.pipeline == PipelineType::GPU) {
        auto gpuSimulator = static_cast<GPUSimulator*>(simulator.get());
        auto gpuRenderer = static_cast<GPURenderer*>(renderer.get());
        gpuSimulator->device = gpuRenderer->device;
        gpuSimulator->queue = gpuRenderer->queue;
    }

    if (!simulator->init(config, imageData, aspectRatio)) {
        std::cerr << "ERR initializing simulator" << std::endl;
        delete imageData;
        if (convertedSurface) SDL_FreeSurface(convertedSurface);
        if (imageSurface) SDL_FreeSurface(imageSurface);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::cout << "Simulator initialized" << std::endl;

    // MAIN LOOP
    bool running = true;
    uint delay = config.simulation.timestep * 1000;

#ifdef __EMSCRIPTEN__
    MainLoopState state{simulator.get(), renderer.get(), window, &running, windowWidth, windowHeight};
    emscripten_set_main_loop_arg(mainLoopCallback, &state, 0, true);
#else
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_MOUSEBUTTONDOWN and event.button.button == SDL_BUTTON_LEFT) {
                std::pair<int, int> gridCoords = simulator->screenToGridCoords(event.button.x, event.button.y, windowWidth, windowHeight);
                if (simulator->isInsideCircle(gridCoords.first, gridCoords.second)) {
                    simulator->onMouseDown(gridCoords.first, gridCoords.second);
                }
            } else if (event.type == SDL_MOUSEBUTTONUP and event.button.button == SDL_BUTTON_LEFT) {
                simulator->onMouseUp();
            } else if (event.type == SDL_MOUSEMOTION and event.motion.state & SDL_BUTTON_LMASK) {
                std::pair<int, int> gridCoords = simulator->screenToGridCoords(event.motion.x, event.motion.y, windowWidth, windowHeight);
                simulator->onMouseDrag(gridCoords.first, gridCoords.second);
            }
        }

        simulator->update();
        renderer->render(*simulator);

        // force realtime
        SDL_Delay(delay);
    }
#endif

#ifndef __EMSCRIPTEN__
    // cleanup
    SDL_DestroyWindow(window);

    delete imageData;
    if (convertedSurface) {
        SDL_FreeSurface(convertedSurface);
    }
    if (imageSurface) {
        SDL_FreeSurface(imageSurface);
    }

    SDL_Quit();
#endif

    return 0;
}
