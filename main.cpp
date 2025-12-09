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

std::unique_ptr<IRenderer> createRenderer(SDL_Window* window, const Config& config) {
    if (config.pipeline == PipelineType::CPU) {
        return std::make_unique<Renderer>(window, config);
    }
    return std::make_unique<WebGPURenderer>(window, config);
}

std::unique_ptr<ISimulator> createSimulator(const Config& config) {
    if (config.pipeline == PipelineType::GPU) {
        return std::make_unique<GPUFluidSimulator>(config);
    }
    return std::make_unique<FluidSimulator>(config);
}

std::pair<int, int> mouseToGridCoords(const SDL_Event& event, int windowWidth, int windowHeight, ISimulator* simulator) {
    int mouseX = event.motion.x;
    int mouseY = event.motion.y;

    // convert screen coordinates to simulator world space coordinates
    float simX = mouseX / static_cast<float>(windowWidth) * simulator->getDomainWidth();
    float simY = (windowHeight - mouseY) / static_cast<float>(windowHeight) * simulator->getDomainHeight();

    int gridX = static_cast<int>(simX / simulator->getCellSize());
    int gridY = static_cast<int>(simY / simulator->getCellSize());

    return {gridX, gridY};
}

int main(int argc, char** argv) {
    // TODO support command line arguments for config file path
    Config config = ConfigLoader::loadConfig("../config.json");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialization error: " << SDL_GetError() << std::endl;
        return 1;
    }

    int windowWidth = config.window.defaultWidth;
    int windowHeight = config.window.defaultHeight;

    // image loading
    SDL_Surface* imageSurface = nullptr;
    SDL_Surface* convertedSurface = nullptr;
    ImageData* imageData = nullptr;

    if (!config.ink.imagePath.empty() && config.rendering.target == 3) {
        imageSurface = IMG_Load(config.ink.imagePath.c_str());
        if (imageSurface) {
            float imageAspectRatio = static_cast<float>(imageSurface->w) / imageSurface->h;

            if (imageAspectRatio > 1.0f) { // landscape
                windowWidth = static_cast<int>(config.window.baseSize * 1.2f);
                windowHeight = static_cast<int>(windowWidth / imageAspectRatio);

                if (windowHeight < 600) {
                    windowHeight = 600;
                    windowWidth = static_cast<int>(windowHeight * imageAspectRatio);
                }
            } else { // portrait
                windowHeight = static_cast<int>(config.window.baseSize * 1.2f);
                windowWidth = static_cast<int>(windowHeight * imageAspectRatio);

                if (windowWidth < 600) {
                    windowWidth = 600;
                    windowHeight = static_cast<int>(windowWidth / imageAspectRatio);
                }
            }

            std::cout << "Window size: " << windowWidth << " by " << windowHeight << std::endl;
            std::cout << "Aspect ratio: " << imageAspectRatio << std::endl;

            // 32-bit RGB
            convertedSurface = SDL_ConvertSurfaceFormat(imageSurface, SDL_PIXELFORMAT_RGB888, 0);
            if (!convertedSurface) {
                std::cerr << "Error: Could not convert image surface: " << SDL_GetError() << std::endl;
                SDL_FreeSurface(imageSurface);
                SDL_Quit();
                return 1;
            }

            imageData = new ImageData();
            imageData->pixels = convertedSurface->pixels;
            imageData->width = convertedSurface->w;
            imageData->height = convertedSurface->h;
            imageData->bytesPerPixel = convertedSurface->format->BytesPerPixel;
            imageData->rShift = convertedSurface->format->Rshift;
            imageData->gShift = convertedSurface->format->Gshift;
            imageData->bShift = convertedSurface->format->Bshift;
        } else {
            std::cerr << "Could not load image " << config.ink.imagePath
                      << ": " << IMG_GetError() << std::endl;
            SDL_Quit();
            return 1;
        }
    } else if (config.rendering.target == 3) {
        std::cerr << "No input image path provided for ink mode" << std::endl;
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
        std::cerr << "Window creation error: " << SDL_GetError() << std::endl;
        delete imageData;
        if (convertedSurface) SDL_FreeSurface(convertedSurface);
        if (imageSurface) SDL_FreeSurface(imageSurface);
        SDL_Quit();
        return 1;
    }

    auto renderer = createRenderer(window, config);
    auto simulator = createSimulator(config);

    if (!renderer->init(config)) {
        std::cerr << "Renderer initialization error" << std::endl;
        delete imageData;
        if (convertedSurface) SDL_FreeSurface(convertedSurface);
        if (imageSurface) SDL_FreeSurface(imageSurface);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // TEMP disabled
    // if (config.pipeline == PipelineType::GPU) {
    //     auto gpuSimulator = static_cast<GPUFluidSimulator*>(simulator.get());
    //     auto gpuRenderer = static_cast<WebGPURenderer*>(renderer.get());

    //     if (!gpuSimulator->initWebGPU(gpuRenderer->getDevice(), gpuRenderer->getQueue())) {
    //         std::cerr << "Error initializing WebGPU device" << std::endl;
    //         exit(1);
    //     }
    // }

    simulator->init(config, imageData);

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_MOUSEBUTTONDOWN and event.button.button == SDL_BUTTON_LEFT) {
                std::pair<int, int> gridCoords = mouseToGridCoords(event, windowWidth, windowHeight, simulator.get());
                if (simulator->isInsideCircle(gridCoords.first, gridCoords.second)) {
                    simulator->onMouseDown(gridCoords.first, gridCoords.second);
                }
            } else if (event.type == SDL_MOUSEBUTTONUP and event.button.button == SDL_BUTTON_LEFT) {
                simulator->onMouseUp();
            } else if (event.type == SDL_MOUSEMOTION and event.motion.state & SDL_BUTTON_LMASK) {
                std::pair<int, int> gridCoords = mouseToGridCoords(event, windowWidth, windowHeight, simulator.get());
                simulator->onMouseDrag(gridCoords.first, gridCoords.second);
            }
        }

        simulator->update();
        renderer->render(*simulator);

        // 60 fps
        SDL_Delay(16);
    }

    renderer->cleanup();
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
