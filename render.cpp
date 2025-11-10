#include "render.h"
#include <algorithm>
#include <cmath>

Renderer::Renderer(SDL_Window* window, bool drawVelocities, int drawTarget)
    : window(window), renderer(nullptr), texture(nullptr), pixels(nullptr),
      drawTarget(drawTarget), drawVelocities(drawVelocities), frameCount(0),
      histogramBins(HISTOGRAM_BINS, 0), histogramMin(0.0f), histogramMax(0.0f),
      velocityHistogramBins(HISTOGRAM_BINS, 0), velocityHistogramMin(0.0f), velocityHistogramMax(0.0f) {

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

void Renderer::render(const ISimulator& simulator) {
    // clear bg
    std::fill(pixels, pixels + windowWidth * windowHeight, 0xFF000000);

    drawFluidField(simulator);

    if (drawVelocities) {
        drawVelocityField(simulator);
    }

    // compute histograms every n frames
    if (frameCount % 2 == 0) {
        computeHistogram(simulator);
        computeVelocityHistogram(simulator);
    }
    
    drawHistogram();
    drawVelocityHistogram();

    frameCount++;

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

void Renderer::mapValueToVelocityColor(float value, float min, float max, Uint8& r, Uint8& g, Uint8& b) {
    value = std::max(min, std::min(max - 0.0001f, value));
    float delta = max - min;
    float normalized = delta == 0.0f ? 0.5f : (value - min) / delta;
    
    if (normalized < 0.5f) {
        float t = normalized * 2.0f;
        r = 255;
        g = static_cast<Uint8>(t * 165.0f);
        b = 0;
    } else {
        float t = (normalized - 0.5f) * 2.0f;
        r = 255;
        g = static_cast<Uint8>(165.0f + t * 90.0f);
        b = 0;
    }
}

void Renderer::setPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b) {
    if (x >= 0 && x < windowWidth && y >= 0 && y < windowHeight) {
        pixels[y * windowWidth + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
}

void Renderer::drawFluidField(const ISimulator& simulator) {
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

void Renderer::drawVelocityField(const ISimulator& simulator) {
    const auto& velocityX = simulator.getVelocityX();
    const auto& velocityY = simulator.getVelocityY();
    const auto& solid = simulator.getSolid();

    float cellSize = simulator.getCellSize();
    int gridX = simulator.getGridX();
    int gridY = simulator.getGridY();

    // velocity vectors in white (normalized to unit length, then scaled)
    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            int idx = j * gridX + i;

            if (solid[idx] != 0.0f) {
                float vx = velocityX[idx];
                float vy = velocityY[idx];
                float magnitude = std::sqrt(vx * vx + vy * vy);
                
                if (magnitude > 0.001f) {
                    float normalizedLength = 0.3f; // CHANGE FOR VELOCITY VECTOR LENGTH
                    vx = (vx / magnitude) * normalizedLength;
                    vy = (vy / magnitude) * normalizedLength;
                }
                
                // horizontal vel component
                int x0, y0;
                convertCoordinates(i * cellSize, (j + 0.5f) * cellSize, x0, y0);
                int x1 = x0 + static_cast<int>(vx * velScale * canvasScale);

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
                int y1 = y0 - static_cast<int>(vy * velScale * canvasScale);

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

void Renderer::computeHistogram(const ISimulator& simulator) {
    const auto& pressure = simulator.getPressure();
    const auto& solid = simulator.getSolid();
    
    int gridX = simulator.getGridX();
    int gridY = simulator.getGridY();
    
    bool first = true;
    for (int i = 0; i < gridX * gridY; i++) {
        // only look at fluid cells
        if (solid[i] != 0.0f) {
            if (first) {
                histogramMin = pressure[i];
                histogramMax = pressure[i];
                first = false;
            } else {
                histogramMin = std::min(histogramMin, pressure[i]);
                histogramMax = std::max(histogramMax, pressure[i]);
            }
        }
    }
    
    std::fill(histogramBins.begin(), histogramBins.end(), 0);
    
    if (histogramMax > histogramMin) {
        float binWidth = (histogramMax - histogramMin) / HISTOGRAM_BINS;
        for (int i = 0; i < gridX * gridY; i++) {
            if (solid[i] != 0.0f) { // fluid cell
                int bin = static_cast<int>((pressure[i] - histogramMin) / binWidth);
                bin = std::max(0, std::min(HISTOGRAM_BINS - 1, bin));
                histogramBins[bin]++;
            }
        }
    }
}

void Renderer::drawHistogram() {
    const int histWidth = 300;
    const int histHeight = 150;
    const int histX = 10;
    const int histY = 10;
    
    int maxCount = 0;
    for (int i = 0; i < HISTOGRAM_BINS; i++) {
        maxCount = std::max(maxCount, histogramBins[i]);
    }
    
    if (maxCount == 0) return;
    
    // background
    for (int y = histY; y < histY + histHeight; y++) {
        for (int x = histX; x < histX + histWidth; x++) {
            if (x >= 0 && x < windowWidth && y >= 0 && y < windowHeight) {
                Uint8 bg = 40;
                setPixel(x, y, bg, bg, bg);
            }
        }
    }
    
    // border
    for (int x = histX; x < histX + histWidth; x++) {
        if (x >= 0 && x < windowWidth) {
            if (histY >= 0 && histY < windowHeight) {
                setPixel(x, histY, 200, 200, 200); // top border
            }
            if (histY + histHeight - 1 >= 0 && histY + histHeight - 1 < windowHeight) {
                setPixel(x, histY + histHeight - 1, 200, 200, 200); // bottom border
            }
        }
    }
    for (int y = histY; y < histY + histHeight; y++) {
        if (y >= 0 && y < windowHeight) {
            if (histX >= 0 && histX < windowWidth) {
                setPixel(histX, y, 200, 200, 200); // left border
            }
            if (histX + histWidth - 1 >= 0 && histX + histWidth - 1 < windowWidth) {
                setPixel(histX + histWidth - 1, y, 200, 200, 200); // right border
            }
        }
    }
    
    int barWidth = histWidth / HISTOGRAM_BINS;
    int padding = 1;
    
    // bars
    for (int i = 0; i < HISTOGRAM_BINS; i++) {
        int barHeight = static_cast<int>((static_cast<float>(histogramBins[i]) / maxCount) * (histHeight - 20));
        int barX = histX + 10 + i * barWidth;
        
        for (int x = barX; x < barX + barWidth - padding && x < histX + histWidth - 10; x++) {
            for (int y = histY + histHeight - 10; y >= histY + histHeight - 10 - barHeight && y >= histY + 10; y--) {
                if (x >= 0 && x < windowWidth && y >= 0 && y < windowHeight) {
                    float normalized = static_cast<float>(i) / HISTOGRAM_BINS;
                    Uint8 r, g, b;
                    mapValueToColor(normalized, 0.0f, 1.0f, r, g, b);
                    setPixel(x, y, r, g, b);
                }
            }
        }
    }
}

void Renderer::computeVelocityHistogram(const ISimulator& simulator) {
    const auto& velocityX = simulator.getVelocityX();
    const auto& velocityY = simulator.getVelocityY();
    const auto& solid = simulator.getSolid();
    
    int gridX = simulator.getGridX();
    int gridY = simulator.getGridY();
    
    bool first = true;
    for (int i = 0; i < gridX * gridY; i++) {
        if (solid[i] != 0.0f) { // fluid cell
            float velMagnitude = std::sqrt(velocityX[i] * velocityX[i] + velocityY[i] * velocityY[i]);
            if (first) {
                velocityHistogramMin = velMagnitude;
                velocityHistogramMax = velMagnitude;
                first = false;
            } else {
                velocityHistogramMin = std::min(velocityHistogramMin, velMagnitude);
                velocityHistogramMax = std::max(velocityHistogramMax, velMagnitude);
            }
        }
    }
    
    std::fill(velocityHistogramBins.begin(), velocityHistogramBins.end(), 0);
    
    if (velocityHistogramMax > velocityHistogramMin) {
        float binWidth = (velocityHistogramMax - velocityHistogramMin) / HISTOGRAM_BINS;
        for (int i = 0; i < gridX * gridY; i++) {
            if (solid[i] != 0.0f) { // fluid cell
                float velMagnitude = std::sqrt(velocityX[i] * velocityX[i] + velocityY[i] * velocityY[i]);
                int bin = static_cast<int>((velMagnitude - velocityHistogramMin) / binWidth);
                bin = std::max(0, std::min(HISTOGRAM_BINS - 1, bin));
                velocityHistogramBins[bin]++;
            }
        }
    }
}

void Renderer::drawVelocityHistogram() {
    const int histWidth = 300;
    const int histHeight = 150;
    const int histX = 320;
    const int histY = 10;
    
    int maxCount = 0;
    for (int i = 0; i < HISTOGRAM_BINS; i++) {
        maxCount = std::max(maxCount, velocityHistogramBins[i]);
    }
    
    if (maxCount == 0) return;

    // background
    for (int y = histY; y < histY + histHeight; y++) {
        for (int x = histX; x < histX + histWidth; x++) {
            if (x >= 0 && x < windowWidth && y >= 0 && y < windowHeight) {
                Uint8 bg = 40;
                setPixel(x, y, bg, bg, bg);
            }
        }
    }
    
    // border
    for (int x = histX; x < histX + histWidth; x++) {
        if (x >= 0 && x < windowWidth) {
            if (histY >= 0 && histY < windowHeight) {
                setPixel(x, histY, 200, 200, 200); // top border
            }
            if (histY + histHeight - 1 >= 0 && histY + histHeight - 1 < windowHeight) {
                setPixel(x, histY + histHeight - 1, 200, 200, 200); // bottom border
            }
        }
    }
    for (int y = histY; y < histY + histHeight; y++) {
        if (y >= 0 && y < windowHeight) {
            if (histX >= 0 && histX < windowWidth) {
                setPixel(histX, y, 200, 200, 200); // left border
            }
            if (histX + histWidth - 1 >= 0 && histX + histWidth - 1 < windowWidth) {
                setPixel(histX + histWidth - 1, y, 200, 200, 200); // right border
            }
        }
    }
    
    int barWidth = histWidth / HISTOGRAM_BINS;
    int padding = 1;

    // bars
    for (int i = 0; i < HISTOGRAM_BINS; i++) {
        int barHeight = static_cast<int>((static_cast<float>(velocityHistogramBins[i]) / maxCount) * (histHeight - 20));
        int barX = histX + 10 + i * barWidth;
        
        for (int x = barX; x < barX + barWidth - padding && x < histX + histWidth - 10; x++) {
            for (int y = histY + histHeight - 10; y >= histY + histHeight - 10 - barHeight && y >= histY + 10; y--) {
                if (x >= 0 && x < windowWidth && y >= 0 && y < windowHeight) {
                    float normalized = static_cast<float>(i) / HISTOGRAM_BINS;
                    Uint8 r, g, b;
                    mapValueToVelocityColor(normalized, 0.0f, 1.0f, r, g, b);
                    setPixel(x, y, r, g, b);
                }
            }
        }
    }
}