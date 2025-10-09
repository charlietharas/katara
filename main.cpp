#define SDL_MAIN_HANDLED

#include <webgpu/webgpu.h>
#include <iostream>
#include <sdl2webgpu.h>
#include <SDL2/SDL.h>
#include "sim.h"
#include "render.h"
#include "gpu_render.h"

const int WINDOW_WIDTH = 1200;
const int WINDOW_HEIGHT = 800;

#define USE_GPU_RENDERER
#define USE_GPU_SIMULATOR

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialization error: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("katara",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          WINDOW_WIDTH,
                                          WINDOW_HEIGHT,
                                          SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "Window creation error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    #if defined(USE_GPU_RENDERER)
    WebGPURenderer renderer(window);
    if (!renderer.init()) {
        std::cerr << "WebGPU renderer initialization error" << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::cout << "Using WebGPU for rendering" << std::endl;
    #else
    Renderer renderer(window);
    if (!renderer.init()) {
        std::cerr << "Renderer initialization error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::cout << "Using CPU for rendering" << std::endl;
    #endif

    FluidSimulator simulator;
    simulator.init();

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        // sim
        simulator.update();

        // render
        renderer.render(simulator);

        // 60 fps
        SDL_Delay(16);
    }

    renderer.cleanup();
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
