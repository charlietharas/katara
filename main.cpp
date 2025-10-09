#define SDL_MAIN_HANDLED

#include <webgpu/webgpu.h>
#include <iostream>
#include <sdl2webgpu.h>
#include <SDL2/SDL.h>
#include "sim.h"
#include "render.h"

const int WINDOW_WIDTH = 1200;
const int WINDOW_HEIGHT = 800;

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

    Renderer renderer(window);
    if (!renderer.init()) {
        std::cerr << "Renderer initialization error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

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
