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

int main(int argc, char** argv) {
    // load config from default path if none specified
    std::string configPath = "../config.json";
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
    SDL_Event event;
    uint delay = config.simulation.timestep * 1000;
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

    return 0;
}
