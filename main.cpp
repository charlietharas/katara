#define SDL_MAIN_HANDLED

#include <webgpu/webgpu.h>
#include <iostream>
#include <sdl2webgpu.h>
#include <SDL2/SDL.h>
#include <string>
#include <memory>
#include "sim.h"
#include "render.h"
#include "gpu_render.h"
#include "gpu_sim.h"
#include "irenderer.h"
#include "isimulator.h"

const int WINDOW_WIDTH = 1200;
const int WINDOW_HEIGHT = 800;

struct SimConfig {
    bool useGPURendering = true;
    bool useGPUSimulation = false;
};

SimConfig parseArgs(int argc, char** argv) {
    SimConfig config;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg.find("--render=") == 0) {
            std::string value = arg.substr(9);
            if (value == "gpu") {
                config.useGPURendering = true;
            } else if (value == "cpu") {
                config.useGPURendering = false;
            } else {
                std::cerr << "Invalid renderer option: " << value << " (use 'gpu' or 'cpu')\n";
                exit(1);
            }
        } else if (arg.find("--sim=") == 0) {
            std::string value = arg.substr(6);
            if (value == "gpu") {
                config.useGPUSimulation = true;
            } else if (value == "cpu") {
                config.useGPUSimulation = false;
            } else {
                std::cerr << "Invalid simulator option: " << value << " (use 'gpu' or 'cpu')\n";
                exit(1);
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  --render=cpu|gpu     Choose renderer\n"
                      << "  --sim=cpu|gpu        Choose simulator\n"
                      << "  --help, -h           Show this help dialog\n";
            exit(0);
        } else {
            std::cerr << "Unknown option: " << arg << "\n"
                      << "Use --help for usage information\n";
            exit(1);
        }
    }

    return config;
}

std::unique_ptr<IRenderer> createRenderer(SDL_Window* window, const SimConfig& config) {
    if (config.useGPURendering) {
        return std::make_unique<WebGPURenderer>(window);
    }
    return std::make_unique<Renderer>(window);
}

std::unique_ptr<ISimulator> createSimulator(const SimConfig& config) {
    if (config.useGPUSimulation) {
        return std::make_unique<GPUFluidSimulator>();
    }

    return std::make_unique<FluidSimulator>();
}

int main(int argc, char** argv) {
    SimConfig config = parseArgs(argc, argv);

    std::cout << "Configuration:\n"
              << "  Rendering: " << (config.useGPURendering ? "GPU" : "CPU") << "\n"
              << "  Simulation: " << (config.useGPUSimulation ? "GPU" : "CPU") << "\n"
              << std::endl;

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

    auto renderer = createRenderer(window, config);
    auto simulator = createSimulator(config);

    if (!renderer->init()) {
        std::cerr << "Renderer initialization error" << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    simulator->init();

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        // sim
        simulator->update();

        // render
        renderer->render(*simulator);

        // 60 fps
        SDL_Delay(16);
    }

    renderer->cleanup();
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
