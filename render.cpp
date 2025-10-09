#include "render.h"
#include <algorithm>
#include <cmath>

Renderer::Renderer(SDL_Window* window)
    : window(window), renderer(nullptr), texture(nullptr), pixels(nullptr),
      drawTarget(2), drawVelocities(false) {

    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    // world coordinates
    simWidth = 1.0f;
    canvasScale = std::min(windowWidth, windowHeight) / simWidth;
    simWidth = windowWidth / canvasScale;
    simHeight = windowHeight / canvasScale;

    // pixel buffer
    pixels = new Uint32[windowWidth * windowHeight];
}

Renderer::~Renderer() {
    cleanup();
    delete[] pixels;
}

bool Renderer::init() {
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        return false;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STREAMING, windowWidth, windowHeight);
    if (!texture) {
        return false;
    }

    return true;
}

void Renderer::cleanup() {
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
}

void Renderer::render(const FluidSimulator& simulator) {
    // clear bg
    std::fill(pixels, pixels + windowWidth * windowHeight, 0xFF000000);

    drawFluidField(simulator);

    if (drawVelocities) {
        drawVelocityField(simulator);
    }

    SDL_UpdateTexture(texture, nullptr, pixels, windowWidth * sizeof(Uint32));

    // render to screen
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

void Renderer::convertCoordinates(float simX, float simY, int& pixelX, int& pixelY) {
    pixelX = static_cast<int>(simX * canvasScale);
    pixelY = windowHeight - static_cast<int>(simY * canvasScale);
}

void Renderer::mapValueToColor(float value, float min, float max, Uint8& r, Uint8& g, Uint8& b) {
    value = std::max(min, std::min(max - 0.0001f, value));
    float delta = max - min;
    float normalized = delta == 0.0f ? 0.5f : (value - min) / delta;

    float m = 0.25f;
    int num = static_cast<int>(normalized / m);
    float s = (normalized - num * m) / m;

    float fr = 0.0f, fg = 0.0f, fb = 0.0f;

    switch (num) {
        case 0: fr = 0.0f; fg = s; fb = 1.0f; break;
        case 1: fr = 0.0f; fg = 1.0f; fb = 1.0f - s; break;
        case 2: fr = s; fg = 1.0f; fb = 0.0f; break;
        case 3: fr = 1.0f; fg = 1.0f - s; fb = 0.0f; break;
    }

    r = static_cast<Uint8>(fr * 255);
    g = static_cast<Uint8>(fg * 255);
    b = static_cast<Uint8>(fb * 255);
}

void Renderer::mapValueToGreyscale(float value, float min, float max, Uint8& r, Uint8& g, Uint8& b) {
    float t = (value - min) / (max - min) * 255.0f;
    t = std::max(0.0f, std::min(255.0f, t));
    r = g = b = static_cast<Uint8>(t);
}

void Renderer::setPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b) {
    if (x >= 0 && x < windowWidth && y >= 0 && y < windowHeight) {
        pixels[y * windowWidth + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
}

void Renderer::drawFluidField(const FluidSimulator& simulator) {
    const auto& pressure = simulator.getPressure();
    const auto& density = simulator.getDensity();
    const auto& solid = simulator.getSolid();

    float cellSize = simulator.getCellSize();
    int gridX = simulator.getGridX();
    int gridY = simulator.getGridY();

    // pressure range
    float minP = pressure[0];
    float maxP = pressure[0];
    for (int i = 0; i < gridX * gridY; i++) {
        minP = std::min(minP, pressure[i]);
        maxP = std::max(maxP, pressure[i]);
    }

    // draw cells
    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            int idx = j * gridX + i;

            if (solid[idx] != 0.0f) {
                Uint8 r, g, b;

                if (drawTarget == 0) {
                    // draw pressure
                    mapValueToColor(pressure[idx], minP, maxP, r, g, b);
                } else if (drawTarget == 1) {
                    // draw smoke/density
                    mapValueToGreyscale(density[idx], 0.0f, 1.0f, r, g, b);
                } else {
                    // draw pretty pressure + smoke
                    float dens = density[idx];
                    mapValueToColor(pressure[idx], minP, maxP, r, g, b);
                    r = std::max(0, static_cast<int>(r) - static_cast<int>(255 * dens));
                    g = std::max(0, static_cast<int>(g) - static_cast<int>(255 * dens));
                    b = std::max(0, static_cast<int>(b) - static_cast<int>(255 * dens));
                }

                // pixel coords
                int x0, y0;
                convertCoordinates(i * cellSize, (j + 1) * cellSize, x0, y0);

                int cellWidth = static_cast<int>(canvasScale * cellSize) + 1;
                int cellHeight = static_cast<int>(canvasScale * cellSize) + 1;

                // fill cell
                for (int yi = y0; yi < y0 + cellHeight && yi < windowHeight; yi++) {
                    for (int xi = x0; xi < x0 + cellWidth && xi < windowWidth; xi++) {
                        setPixel(xi, yi, r, g, b);
                    }
                }
            } else {
                // boundaries in grey
                int x0, y0;
                convertCoordinates(i * cellSize, (j + 1) * cellSize, x0, y0);

                int cellWidth = static_cast<int>(canvasScale * cellSize) + 1;
                int cellHeight = static_cast<int>(canvasScale * cellSize) + 1;

                for (int yi = y0; yi < y0 + cellHeight && yi < windowHeight; yi++) {
                    for (int xi = x0; xi < x0 + cellWidth && xi < windowWidth; xi++) {
                        setPixel(xi, yi, 125, 125, 125);
                    }
                }
            }
        }
    }
}

void Renderer::drawVelocityField(const FluidSimulator& simulator) {
    const auto& velocityX = simulator.getVelocityX();
    const auto& velocityY = simulator.getVelocityY();
    const auto& solid = simulator.getSolid();

    float cellSize = simulator.getCellSize();
    int gridX = simulator.getGridX();
    int gridY = simulator.getGridY();

    // velocity vectors in white
    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            int idx = j * gridX + i;

            if (solid[idx] != 0.0f) {
                // horizontal vel component
                int x0, y0;
                convertCoordinates(i * cellSize, (j + 0.5f) * cellSize, x0, y0);
                int x1 = x0 + static_cast<int>(velocityX[idx] * velScale * canvasScale);

                // approx velocity line with pixels
                int steps = std::abs(x1 - x0);
                if (steps > 0) {
                    for (int step = 0; step <= steps; step++) {
                        int x = x0 + (x1 - x0) * step / steps;
                        setPixel(x, y0, 255, 255, 255);
                    }
                }

                // vertical vel component
                convertCoordinates((i + 0.5f) * cellSize, j * cellSize, x0, y0);
                int y1 = y0 - static_cast<int>(velocityY[idx] * velScale * canvasScale);

                steps = std::abs(y1 - y0);
                if (steps > 0) {
                    for (int step = 0; step <= steps; step++) {
                        int y = y0 + (y1 - y0) * step / steps;
                        setPixel(x0, y, 255, 255, 255);
                    }
                }
            }
        }
    }
}