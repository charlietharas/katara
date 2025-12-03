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

const int BASE_WINDOW_SIZE = 800; // smaller dimension

struct SimConfig {
    bool useGPURendering = true;
    bool useGPUSimulation = false;
    bool drawVelocities = false;
    int drawTarget = 2; // 0=pressure, 1=smoke, 2=both, 3=ink
    bool disableHistograms = false;
    std::string imagePath; // input image for ink diffusion
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
        } else if (arg == "--nh") {
            config.disableHistograms = true;
        } else if (arg.find("--image=") == 0) {
            config.imagePath = arg.substr(8);
        } else if (arg.find("--target=") == 0) {
            std::string value = arg.substr(9);
            if (value == "pressure") {
                config.drawTarget = 0;
            } else if (value == "smoke") {
                config.drawTarget = 1;
            } else if (value == "both") {
                config.drawTarget = 2;
            } else if (value == "ink") {
                config.drawTarget = 3;
            } else {
                std::cerr << "Invalid target option: " << value << " (use 'pressure', 'smoke', 'both', or 'ink')\n";
                exit(1);
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  --render=cpu|gpu                        Choose renderer\n"
                      << "  --sim=cpu|gpu                           Choose simulator\n"
                      << "  --vel                                   Show velocity vectors\n"
                      << "  --target=pressure|smoke|both|ink        Choose visualization target\n"
                      << "  --nh                                    Disable histograms\n"
                      << "  --image=<path>                          Load image for ink diffusion (enables dynamic resizing)\n"
                      << "  --help, -h                              Show this help dialog\n";
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
    return std::make_unique<Renderer>(window, config.drawVelocities, config.drawTarget, config.disableHistograms);
}

std::unique_ptr<ISimulator> createSimulator(const SimConfig& config) {
    if (config.useGPUSimulation) {
        return std::make_unique<GPUFluidSimulator>();
    }
    return std::make_unique<FluidSimulator>();
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
    SimConfig config = parseArgs(argc, argv);

    std::string targetName = config.drawTarget == 0 ? "pressure" :
                             (config.drawTarget == 1 ? "smoke" :
                             (config.drawTarget == 2 ? "both" : "ink"));

    std::cout << "Configuration:\n"
              << "  Rendering: " << (config.useGPURendering ? "GPU" : "CPU") << "\n"
              << "  Simulation: " << (config.useGPUSimulation ? "GPU" : "CPU") << "\n"
              << "  Velocity vectors: " << (config.drawVelocities ? "Enabled" : "Disabled") << "\n"
              << "  Visualization target: " << targetName << "\n"
              << "  Disable histograms: " << (config.disableHistograms ? "Disabled" : "Enabled") << "\n"
              << std::endl;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialization error: " << SDL_GetError() << std::endl;
        return 1;
    }

    int windowWidth = 1200; // defaults
    int windowHeight = 800;

    if (!config.imagePath.empty() && config.drawTarget == 3) {
        // get image dimensions
        SDL_Surface* tempImage = IMG_Load(config.imagePath.c_str());
        if (tempImage) {
            float imageAspectRatio = static_cast<float>(tempImage->w) / tempImage->h;

            if (imageAspectRatio > 1.0f) { // landscape
                windowWidth = static_cast<int>(BASE_WINDOW_SIZE * 1.2f);
                windowHeight = static_cast<int>(windowWidth / imageAspectRatio);

                if (windowHeight < 600) {
                    windowHeight = 600;
                    windowWidth = static_cast<int>(windowHeight * imageAspectRatio);
                }
            } else { // portrait
                windowHeight = static_cast<int>(BASE_WINDOW_SIZE * 1.2f);
                windowWidth = static_cast<int>(windowHeight * imageAspectRatio);

                if (windowWidth < 600) {
                    windowWidth = 600;
                    windowHeight = static_cast<int>(windowWidth / imageAspectRatio);
                }
            }

            std::cout << "Window size: " << windowWidth << " by " << windowHeight << std::endl;
            std::cout << "Aspect ratio: " << imageAspectRatio << std::endl;

            SDL_FreeSurface(tempImage);
        } else {
            std::cerr << "Warning: Could not load image " << config.imagePath
                      << " for window sizing: " << IMG_GetError() << std::endl;
        }
    }

    SDL_Window* window = SDL_CreateWindow("katara",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          windowWidth,
                                          windowHeight,
                                          SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "Window creation error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    auto renderer = createRenderer(window, config);
    auto simulator = createSimulator(config);

    bool imageLoaded = false;
    if (config.drawTarget == 3) {
        Renderer* cpuRenderer = dynamic_cast<Renderer*>(renderer.get());
        if (cpuRenderer) {
            std::string imagePath = config.imagePath.empty() ? "" : config.imagePath;

            if (!imagePath.empty()) {
                if (cpuRenderer->loadInputImage(imagePath)) {
                    cpuRenderer->setResolutionFromImage(*simulator);
                    imageLoaded = true;
                }
            } else {
                std::cerr << "No input image path provided" << std::endl;
                exit(1);
            }
        }
    }

    if (!renderer->init()) {
        std::cerr << "Renderer initialization error" << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    simulator->init(imageLoaded);

    if (imageLoaded) {
        Renderer* cpuRenderer = dynamic_cast<Renderer*>(renderer.get());
        if (cpuRenderer) {
            cpuRenderer->initSimulatorInk(*simulator);
        }
    }

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
    SDL_Quit();

    return 0;
}
