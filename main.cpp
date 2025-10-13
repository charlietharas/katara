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
    bool drawVelocities = false;
    int drawTarget = 2; // 0=pressure, 1=smoke, 2=both
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
        } else if (arg == "--vel") {
            config.drawVelocities = true;
        } else if (arg.find("--target=") == 0) {
            std::string value = arg.substr(9);
            if (value == "pressure") {
                config.drawTarget = 0;
            } else if (value == "smoke") {
                config.drawTarget = 1;
            } else if (value == "both") {
                config.drawTarget = 2;
            } else {
                std::cerr << "Invalid target option: " << value << " (use 'pressure', 'smoke', or 'both')\n";
                exit(1);
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  --render=cpu|gpu              Choose renderer\n"
                      << "  --sim=cpu|gpu                 Choose simulator\n"
                      << "  --vel                         Show velocity vectors\n"
                      << "  --target=pressure|smoke|both  Choose visualization target\n"
                      << "  --help, -h                    Show this help dialog\n";
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
        return std::make_unique<WebGPURenderer>(window, config.drawVelocities, config.drawTarget);
    }
    return std::make_unique<Renderer>(window, config.drawVelocities, config.drawTarget);
}

std::unique_ptr<ISimulator> createSimulator(const SimConfig& config) {
    if (config.useGPUSimulation) {
        return std::make_unique<GPUFluidSimulator>();
    }

    return std::make_unique<FluidSimulator>();
}

int main(int argc, char** argv) {
    SimConfig config = parseArgs(argc, argv);

    std::string targetName = config.drawTarget == 0 ? "pressure" :
                             (config.drawTarget == 1 ? "smoke" : "both");

    std::cout << "Configuration:\n"
              << "  Rendering: " << (config.useGPURendering ? "GPU" : "CPU") << "\n"
              << "  Simulation: " << (config.useGPUSimulation ? "GPU" : "CPU") << "\n"
              << "  Velocity vectors: " << (config.drawVelocities ? "Enabled" : "Disabled") << "\n"
              << "  Visualization target: " << targetName << "\n"
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
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int mouseX = event.button.x;
                    int mouseY = event.button.y;

                    // convert screen coordinates to grid coordinates
                    float simX = mouseX / static_cast<float>(WINDOW_WIDTH) * 1.5f;
                    float simY = (WINDOW_HEIGHT - mouseY) / static_cast<float>(WINDOW_HEIGHT) * 1.0f;

                    int gridX = static_cast<int>(simX / simulator->getCellSize());
                    int gridY = static_cast<int>(simY / simulator->getCellSize());

                    simulator->onMouseDown(gridX, gridY);
                }
            } else if (event.type == SDL_MOUSEBUTTONUP) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    simulator->onMouseUp();
                }
            } else if (event.type == SDL_MOUSEMOTION) {
                if (event.motion.state & SDL_BUTTON_LMASK) {
                    int mouseX = event.motion.x;
                    int mouseY = event.motion.y;

                    // convert screen coordinates to grid coordinates
                    float simX = mouseX / static_cast<float>(WINDOW_WIDTH) * 1.5f;
                    float simY = (WINDOW_HEIGHT - mouseY) / static_cast<float>(WINDOW_HEIGHT) * 1.0f;

                    int gridX = static_cast<int>(simX / simulator->getCellSize());
                    int gridY = static_cast<int>(simY / simulator->getCellSize());

                    simulator->onMouseDrag(gridX, gridY);
                }
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
