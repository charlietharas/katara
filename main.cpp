#define SDL_MAIN_HANDLED

#include <webgpu/webgpu.h>
#include <iostream>
#include <cstdio>
#include <sdl2webgpu.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <memory>
#include "sim_cpu.h"
#include "sim_gpu.h"
#include "sim_shared.h"
#include "render.h"
#include "config.h"
#include "circle_state.h"

// web version imports
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#endif

// this fella is rather important
Config g_config;

std::unique_ptr<Renderer> createRenderer(SDL_Window* window, const Config& config) {
    return std::make_unique<Renderer>(window, config);
}

std::unique_ptr<ISimulator> createSimulator(const Config& config) {
    if (config.pipeline == PipelineType::GPU) {
        return std::make_unique<GPUSimulator>(config);
    }
    return std::make_unique<Simulator>(config);
}


// viewport helpers
constexpr int NUM_VIEWPORTS = 12;

void setViewportTargetByIndex(int index, int target) {
    const std::string name = "viewport_" + std::to_string(index + 1);
    auto it = g_config.layout.components.find(name);
    if (it != g_config.layout.components.end()) {
        it->second.viewportTarget = target;
    } else {
        std::cerr << "ERR finding viewport " << name << " in config" << std::endl;
    }
}

void cycleViewportTargetByNumber(int number, bool forward) {
    const std::string name = "viewport_" + std::to_string(number);
    auto it = g_config.layout.components.find(name);
    if (it == g_config.layout.components.end()) {
        return;
    }
    int& target = it->second.viewportTarget;
    if (forward) {
        target = (target + 1) % NUM_VIEWPORTS;
    } else {
        target = (target - 1 + NUM_VIEWPORTS) % NUM_VIEWPORTS;
    }
}


// layout helpers
void cycleLayoutPreset(bool forward) {
    int index = 0;
    for (int i = 0; i < NUM_LAYOUT_PRESETS; ++i) {
        if (g_config.layout.preset == LAYOUT_PRESETS[i]) {
            index = i;
            break;
        }
    }
    if (forward) {
        index = (index + 1) % NUM_LAYOUT_PRESETS;
    } else {
        index = (index - 1 + NUM_LAYOUT_PRESETS) % NUM_LAYOUT_PRESETS;
    }
    g_config.layout.preset = LAYOUT_PRESETS[index];
}


// image loading helpers
struct LoadedImage {
    SDL_Surface* surface = nullptr;
    SDL_Surface* converted = nullptr;
    ImageData* imageData = nullptr;
    float aspectRatio = DEFAULT_PLOT_ASPECT_RATIO;

    void cleanup() {
        delete imageData;
        if (converted) SDL_FreeSurface(converted);
        if (surface) SDL_FreeSurface(surface);
        imageData = nullptr;
        converted = nullptr;
        surface = nullptr;
    }
};

LoadedImage loadImage(const std::string& path) {
    LoadedImage result;

    SDL_Surface* imageSurface = IMG_Load(path.c_str());
    if (!imageSurface) {
        std::cerr << "ERR loading image (path: " << path << "): " << IMG_GetError() << std::endl;
        return result;
    }

    result.surface = imageSurface;
    result.aspectRatio = static_cast<float>(imageSurface->w) / imageSurface->h;

    // convert to 32-bit RGB
    SDL_Surface* convertedSurface = SDL_ConvertSurfaceFormat(imageSurface, SDL_PIXELFORMAT_RGB888, 0);
    if (!convertedSurface) {
        std::cerr << "ERR converting image surface: " << SDL_GetError() << std::endl;
        SDL_FreeSurface(imageSurface);
        return result;
    }

    result.converted = convertedSurface;

    // copy data to struct
    ImageData* imageData = new ImageData();
    imageData->pixels = convertedSurface->pixels;
    imageData->width = convertedSurface->w;
    imageData->height = convertedSurface->h;
    imageData->bytesPerPixel = convertedSurface->format->BytesPerPixel;
    imageData->rShift = convertedSurface->format->Rshift;
    imageData->gShift = convertedSurface->format->Gshift;
    imageData->bShift = convertedSurface->format->Bshift;
    result.imageData = imageData;

    std::cout << "Loaded image: " << path << " (" << imageData->width << "x" << imageData->height << ")" << std::endl;

    return result;
}

// web build
#ifdef __EMSCRIPTEN__
static bool g_simulationPaused = false;
static Renderer* g_renderer = nullptr;

struct MainLoopState {
    ISimulator* simulator;
    Renderer* renderer;
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
    }

    if (!g_simulationPaused) {
        s->simulator->update();
    }
    s->renderer->render(*s->simulator);
}

extern "C" {
    static ISimulator* g_simulator = nullptr;

    // wasm export helpers
    static void applyInputModeTransition(InputMode prevMode, InputMode newMode) {
        if (!g_simulator) return;

        const bool wasMouse = isMouseInput(prevMode);
        const bool isMouse = isMouseInput(newMode);
        if (isMouse && !wasMouse) {
            g_simulator->mouseCircleX = g_simulator->gridX / 2;
            g_simulator->mouseCircleY = g_simulator->gridY / 2;
            g_simulator->mousePrevCircleX = g_simulator->mouseCircleX;
            g_simulator->mousePrevCircleY = g_simulator->mouseCircleY;
            g_simulator->mouseCircleVelX = 0.0f;
            g_simulator->mouseCircleVelY = 0.0f;
            g_simulator->isMouseDragging = false;
        } else if (!isMouse) {
            g_simulator->isMouseDragging = false;
        }
    }

    static char* duplicateShaderString(const std::string& value) {
        char* out = static_cast<char*>(std::malloc(value.size() + 1));
        if (!out) {
            return nullptr;
        }
        std::memcpy(out, value.c_str(), value.size() + 1);
        return out;
    }

    // lifecycle
    EMSCRIPTEN_KEEPALIVE
    void setSimulatorPointer(void* ptr) {
        g_simulator = static_cast<ISimulator*>(ptr);
    }

    EMSCRIPTEN_KEEPALIVE
    void setSimulationPaused(int paused) {
        g_simulationPaused = (paused != 0);
    }

    // sim domain
    EMSCRIPTEN_KEEPALIVE
    float getSimDomainWidth() {
        if (g_simulator) return g_simulator->domainWidth;
        return 0.0f;
    }

    EMSCRIPTEN_KEEPALIVE
    float getSimDomainHeight() {
        if (g_simulator) return g_simulator->domainHeight;
        return 0.0f;
    }

    EMSCRIPTEN_KEEPALIVE
    float getSimCellSize() {
        if (g_simulator) return g_simulator->cellSize;
        return 0.0f;
    }

    // simulation
    EMSCRIPTEN_KEEPALIVE
    void update() {
        if (g_simulator && !g_simulationPaused) {
            g_simulator->update();
        }
    }

    EMSCRIPTEN_KEEPALIVE
    void updateFingertips(const FingertipData* fingertips, int count) {
        if (g_simulator && !g_simulationPaused) {
            g_simulator->updateCircles(fingertips, count);
        }
    }

    EMSCRIPTEN_KEEPALIVE
    void updateLineSegments(const FingertipData* landmarks, int count) {
        if (g_simulator && !g_simulationPaused) {
            g_simulator->updateLineSegments(landmarks, count);
        }
    }

    // input mode
    EMSCRIPTEN_KEEPALIVE
    void setInputMode(int modeInt) {
        if (!g_simulator) return;

        InputMode newMode;
        switch (modeInt) {
            case 0: newMode = InputMode::Hand; break;
            case 1: newMode = InputMode::MousePull; break;
            default: return;
        }

        const InputMode prevMode = g_config.inputMode;
        g_config.inputMode = newMode;
        applyInputModeTransition(prevMode, newMode);
    }

    // mouse input
    EMSCRIPTEN_KEEPALIVE
    void onMouseDown(int gridX, int gridY) {
        if (g_simulator && isMouseInput(g_config.inputMode)) {
            g_simulator->isMouseDragging = true;
            g_simulator->mouseCircleX = gridX;
            g_simulator->mouseCircleY = gridY;
            g_simulator->mousePrevCircleX = gridX;
            g_simulator->mousePrevCircleY = gridY;
            g_simulator->mouseCircleVelX = 0.0f;
            g_simulator->mouseCircleVelY = 0.0f;
            g_simulator->clearMousePullFootprint();
        }
    }

    EMSCRIPTEN_KEEPALIVE
    void onMouseUp() {
        if (g_simulator) {
            if (isMouseInput(g_config.inputMode)) {
                g_simulator->clearMousePullFootprint();
            }
            g_simulator->isMouseDragging = false;
            g_simulator->mouseCircleVelX = 0.0f;
            g_simulator->mouseCircleVelY = 0.0f;
            g_simulator->mousePrevCircleX = g_simulator->mouseCircleX;
            g_simulator->mousePrevCircleY = g_simulator->mouseCircleY;
        }
    }

    EMSCRIPTEN_KEEPALIVE
    void onMouseDrag(int gridX, int gridY) {
        if (g_simulator && isMouseInput(g_config.inputMode) && g_simulator->isMouseDragging) {
            int r = g_simulator->mouseCircleRadius;
            int newX = std::max(r, std::min(gridX, g_simulator->gridX - r - 1));
            int newY = std::max(r, std::min(gridY, g_simulator->gridY - r - 1));
            if (newX != g_simulator->mouseCircleX || newY != g_simulator->mouseCircleY) {
                g_simulator->moveCircle(newX, newY);
            }
        }
    }

    // layout
    EMSCRIPTEN_KEEPALIVE
    void initLayout(int canvasW, int canvasH, float inkAspectRatio, float cameraAspectRatio) {
        const bool isInkMode = configUsesInkAspect(g_config);
        std::string json = ConfigLoader::computeLayout(
            g_config.layout,
            canvasW,
            canvasH,
            isInkMode,
            inkAspectRatio,
            cameraAspectRatio
        );
        // Write to virtual FS for JS to read
        FILE* f = fopen("/layout_pixels.json", "w");
        if (f) {
            fputs(json.c_str(), f);
            fclose(f);
        }
    }

    // config and fluid
    EMSCRIPTEN_KEEPALIVE
    void resetFluidField() {
        if (!g_simulator) {
            return;
        }
        g_simulator->updateSimParams(g_config);

        if (g_renderer) {
            g_renderer->resetEntropyTimeSeries();
        }

        if (!g_config.imagePath.empty()) {
            LoadedImage img = loadImage(g_config.imagePath);
            if (img.imageData) {
                g_simulator->reinitInk(img.imageData);
                img.cleanup();
                return;
            }
            img.cleanup();
        }

        g_simulator->resetFluidState(true);
        std::cout << "Fluid field reset" << std::endl;    
    }

    EMSCRIPTEN_KEEPALIVE
    void reloadConfig(int flags) {
        const InputMode prevMode = g_config.inputMode;
        Config newConfig = ConfigLoader::loadConfig("/config.json");

        g_config = newConfig;
        applyInputModeTransition(prevMode, g_config.inputMode);

        // sim params
        if (flags & 2) {
            resetFluidField();
        }

        // ink
        if (flags & 1) {
            if (g_simulator && !g_config.imagePath.empty()) {
                LoadedImage img = loadImage(g_config.imagePath);
                if (img.imageData) {
                    g_simulator->reinitInk(img.imageData);
                    std::cout << "Ink reinitialized from: " << g_config.imagePath << std::endl;
                }
                img.cleanup();
            }
        }
    }

    // viewport
    EMSCRIPTEN_KEEPALIVE
    void setViewportTarget(int viewportIndex, int target) {
        setViewportTargetByIndex(viewportIndex, target);
    }

    EMSCRIPTEN_KEEPALIVE
    void setViewportVelocity(int viewportIndex, int enabled) {
        std::string vpName = "viewport_" + std::to_string(viewportIndex + 1);
        auto it = g_config.layout.components.find(vpName);
        if (it != g_config.layout.components.end()) {
            it->second.viewportVelocityViewEnabled = (enabled != 0);
        } else {
            std::cerr << "ERR (velocity) finding viewport " << vpName << " in config" << std::endl;
        }
    }

    EMSCRIPTEN_KEEPALIVE
    void setViewportRotation(int viewportIndex, int rotation) {
        std::string vpName = "viewport_" + std::to_string(viewportIndex + 1);
        auto it = g_config.layout.components.find(vpName);
        if (it != g_config.layout.components.end()) {
            it->second.rotation = ((rotation % 4) + 4) % 4;
        } else {
            std::cerr << "ERR (rotate) finding viewport " << vpName << " in config" << std::endl;
        }
    }

    // view shaders
    EMSCRIPTEN_KEEPALIVE
    char* getViewSource(int index) {
        if (!g_renderer) {
            return duplicateShaderString("");
        }
        return duplicateShaderString(g_renderer->getViewSource(index));
    }

    EMSCRIPTEN_KEEPALIVE
    int setViewSource(int index, const char* wgsl) {
        if (!g_renderer || !wgsl) {
            return 0;
        }
        return g_renderer->setViewSource(index, wgsl) ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE
    int resetViewSource(int index) {
        if (!g_renderer) {
            return 0;
        }
        return g_renderer->resetViewSource(index) ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE
    int applyViewShaders() {
        if (!g_renderer) {
            return 1;
        }
        return g_renderer->applyViewShaders() ? 0 : 1;
    }

    EMSCRIPTEN_KEEPALIVE
    char* getLastShaderError() {
        if (!g_renderer) {
            return duplicateShaderString("Renderer unavailable");
        }
        return duplicateShaderString(g_renderer->getLastShaderError());
    }

    EMSCRIPTEN_KEEPALIVE
    void freeShaderString(char* value) {
        std::free(value);
    }

    // lightmode helper
    EMSCRIPTEN_KEEPALIVE
    void setBackgroundColor(float r, float g, float b) {
        if (g_renderer) {
            g_renderer->setBackgroundColor(r, g, b);
        }
    }
}
#endif

#ifndef __EMSCRIPTEN__
static void initLayoutFromWindow(int w, int h, float inkAspect) {
    ConfigLoader::computeLayout(
        g_config.layout,
        w,
        h,
        configUsesInkAspect(g_config),
        inkAspect,
        DEFAULT_CAMERA_ASPECT_RATIO
    );
}

#endif

int main(int argc, char** argv) {
    // load config from default path if none specified
#ifdef __EMSCRIPTEN__
    std::string configPath = "/config.json"; // emscripten virtual FS root
#else
    std::string configPath = "../config.json"; // desktop/config.json
#endif

    if (argc > 1) {
        configPath = argv[1];
    }
    Config config = ConfigLoader::loadConfig(configPath);
    g_config = config;

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
    if (!config.imagePath.empty()) {
        const int imgInitFlags = IMG_INIT_PNG;
        if ((IMG_Init(imgInitFlags) & imgInitFlags) == 0) {
            std::cerr << "ERR initializing SDL_image PNG support: " << IMG_GetError() << std::endl;
            SDL_Quit();
            return 1;
        }

        // load an image and grab its dimensions for window sizing
        imageSurface = IMG_Load(config.imagePath.c_str());
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
            std::cerr << "ERR loading image (path: " << config.imagePath << "): " << IMG_GetError() << std::endl;
            SDL_Quit();
            return 1;
        }
    } else if (layoutHasInkViewport(config.layout)) {
        std::cerr << "ERR ink viewport configured but no imagePath was provided" << std::endl;
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

#ifndef __EMSCRIPTEN__
    const float inkAspectRatio = !config.imagePath.empty() ? aspectRatio : 1.0f;
    initLayoutFromWindow(windowWidth, windowHeight, inkAspectRatio);
#endif

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
        gpuSimulator->device = renderer->device;
        gpuSimulator->queue = renderer->queue;
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

#ifdef __EMSCRIPTEN__
    // C++ initialization complete
    emscripten_run_script("if (window.kataraOnReady) window.kataraOnReady();");
#endif

    // MAIN LOOP
    bool running = true;
    uint delay = config.simulation.timestep * 1000;

#ifdef __EMSCRIPTEN__
    setSimulatorPointer(simulator.get());
    g_renderer = renderer.get();
    MainLoopState state{simulator.get(), renderer.get(), window, &running, windowWidth, windowHeight};
    emscripten_set_main_loop_arg(mainLoopCallback, &state, 0, true);
#else
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_WINDOWEVENT &&
                       event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                windowWidth = event.window.data1;
                windowHeight = event.window.data2;
                initLayoutFromWindow(windowWidth, windowHeight, inkAspectRatio);
                renderer->onWindowResize(windowWidth, windowHeight);
            } else if (event.type == SDL_KEYDOWN) {
                const SDL_Keymod mod = static_cast<SDL_Keymod>(event.key.keysym.mod);
                if (!(mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) {
                    const bool forward = !(mod & KMOD_SHIFT);
                    bool layoutChanged = false;

                    switch (event.key.keysym.sym) {
                        case SDLK_1:
                        case SDLK_KP_1: // 1
                            cycleViewportTargetByNumber(1, forward);
                            layoutChanged = true;
                            break;
                        case SDLK_2:
                        case SDLK_KP_2: // 2
                            cycleViewportTargetByNumber(2, forward);
                            layoutChanged = true;
                            break;
                        case SDLK_3:
                        case SDLK_KP_3: // 3
                            cycleViewportTargetByNumber(3, forward);
                            layoutChanged = true;
                            break;
                        case SDLK_4:
                        case SDLK_KP_4: // 4
                            cycleViewportTargetByNumber(4, forward);
                            layoutChanged = true;
                            break;
                        case SDLK_l: // L (lowercase)
                            cycleLayoutPreset(forward);
                            layoutChanged = true;
                            break;
                        default:
                            break;
                    }

                    if (layoutChanged) {
                        initLayoutFromWindow(windowWidth, windowHeight, inkAspectRatio);
                    }
                }
            } else if (isMouseInput(g_config.inputMode)) {
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                    auto coords = simulator->viewportAwareScreenToGrid(event.button.x, event.button.y, windowWidth, windowHeight);
                    if (coords.first >= 0) {
                        simulator->onMouseDown(coords.first, coords.second);
                    }
                } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                    simulator->onMouseUp();
                } else if (event.type == SDL_MOUSEMOTION && event.motion.state & SDL_BUTTON_LMASK) {
                    auto coords = simulator->viewportAwareScreenToGrid(event.motion.x, event.motion.y, windowWidth, windowHeight);
                    if (coords.first >= 0) {
                        simulator->onMouseDrag(coords.first, coords.second);
                    }
                }
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
#endif

    return 0;
}
