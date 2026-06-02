#include "render.h"
#include "sim.h"
#include <algorithm>
#include <cmath>
#include <iostream>

Renderer::Renderer(SDL_Window* window, const Config& config)
    :
    window(window),
    frameCount(0),

    // histograms
    densityHistogramBins(IRenderer::HISTOGRAM_BINS, 0),
    densityHistogramMin(0.0f),
    densityHistogramMax(0.0f),
    velocityHistogramBins(IRenderer::HISTOGRAM_BINS, 0),
    velocityHistogramMin(0.0f),
    velocityHistogramMax(0.0f)
{
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    // world coordinates set after simulator is available
    simWidth = 1.0f;
    simHeight = 1.0f;
    canvasScale = std::min(windowWidth, windowHeight);

    // pixel buffer
    pixels = new Uint32[windowWidth * windowHeight];
}

Renderer::~Renderer() {
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    delete[] pixels;
}


// MAIN RENDER LOOP
bool Renderer::init(const Config& config) {
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

void Renderer::render(const ISimulator& simulator) {
    // Copy rendering config from g_config
    const int drawTarget = g_config.rendering.target;
    const bool drawVelocities = g_config.rendering.showVelocityVectors;
    const bool disableHistograms = g_config.rendering.disableHistograms;
    const float velScale = g_config.rendering.velocityScale;

    simWidth = simulator.domainWidth;
    simHeight = simulator.domainHeight;
    float scaleX = windowWidth / simWidth;
    float scaleY = windowHeight / simHeight;
    canvasScale = std::min(scaleX, scaleY);

    // clear bg
    std::fill(pixels, pixels + windowWidth * windowHeight, 0xFF050505);

    drawFluidField(simulator, drawTarget);
    if (drawVelocities) {
        drawVelocityField(simulator, velScale);
    }

    // compute histograms every n frames
    int histogramFrameInterval = 1;
    if (!disableHistograms && frameCount++ % histogramFrameInterval == 0) {
        computeHistograms(simulator);
    }

    // draw histograms every frame
    if (!disableHistograms) {
        drawHistograms();
    }

    SDL_UpdateTexture(texture, nullptr, pixels, windowWidth * sizeof(Uint32));

    // render to screen
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}


// RENDER HELPERS
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
        default: fr = 1.0f; fg = 0.0f; fb = 0.0f; break;
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

void Renderer::mapValueToHeatmap(float value, float min, float max, Uint8& r, Uint8& g, Uint8& b) {
    float t = (value - min) / (max - min);
    t = std::max(0.0f, std::min(1.0f, t));

    float fr = 0.0f;
    float fg = 0.0f;
    float fb = 0.0f;

    if (t < 0.33f) {
        float k = t / 0.33f;
        fr = 0.0f;
        fg = k;
        fb = 1.0f;
    } else if (t < 0.66f) {
        float k = (t - 0.33f) / 0.33f;
        fr = k;
        fg = 1.0f;
        fb = 1.0f - k;
    } else {
        float k = (t - 0.66f) / 0.34f;
        fr = 1.0f;
        fg = 1.0f - 0.75f * k;
        fb = 0.0f;
    }

    r = static_cast<Uint8>(std::max(0.0f, std::min(255.0f, fr * 255.0f)));
    g = static_cast<Uint8>(std::max(0.0f, std::min(255.0f, fg * 255.0f)));
    b = static_cast<Uint8>(std::max(0.0f, std::min(255.0f, fb * 255.0f)));
}

void Renderer::mapDivergenceDebug(float divergence, float scale, Uint8& r, Uint8& g, Uint8& b) {
    float normalized = divergence / std::max(scale, 1e-4f);
    normalized = std::max(-1.0f, std::min(1.0f, normalized));
    float magnitude = std::pow(std::abs(normalized), 0.65f);

    float baseR = 0.02f;
    float baseG = 0.02f;
    float baseB = 0.025f;

    float tintR = normalized >= 0.0f ? 1.0f : 0.12f;
    float tintG = normalized >= 0.0f ? 0.24f : 0.38f;
    float tintB = normalized >= 0.0f ? 0.14f : 1.0f;

    float outR = std::max(0.0f, std::min(1.0f, baseR + tintR * magnitude));
    float outG = std::max(0.0f, std::min(1.0f, baseG + tintG * magnitude));
    float outB = std::max(0.0f, std::min(1.0f, baseB + tintB * magnitude));

    r = static_cast<Uint8>(outR * 255.0f);
    g = static_cast<Uint8>(outG * 255.0f);
    b = static_cast<Uint8>(outB * 255.0f);
}

void Renderer::mapValueToVelocityColor(float value, float min, float max, Uint8& r, Uint8& g, Uint8& b) {
    value = std::max(min, std::min(max - 0.0001f, value));
    float delta = max - min;
    float normalized = delta == 0.0f ? 0.5f : (value - min) / delta;
    
    if (normalized < 0.5f) {
        float t = normalized * 2.0f;
        r = 255;
        g = static_cast<Uint8>(t * 165.0f); // orange to yellow
        b = 0;
    } else {
        float t = (normalized - 0.5f) * 2.0f;
        r = 255;
        g = static_cast<Uint8>(165.0f + t * 90.0f); // yellow to white
        b = 0;
    }
}

void Renderer::mapInkToColor(float r, float g, float b, Uint8& outR, Uint8& outG, Uint8& outB) {
    r = std::max(0.0f, std::min(1.0f, r));
    g = std::max(0.0f, std::min(1.0f, g));
    b = std::max(0.0f, std::min(1.0f, b));

    outR = static_cast<Uint8>(std::max(0.0f, std::min(255.0f, r * 255.0f)));
    outG = static_cast<Uint8>(std::max(0.0f, std::min(255.0f, g * 255.0f)));
    outB = static_cast<Uint8>(std::max(0.0f, std::min(255.0f, b * 255.0f)));
}

void Renderer::setPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b) {
    if (x >= 0 && x < windowWidth && y >= 0 && y < windowHeight) {
        pixels[y * windowWidth + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
}

void Renderer::drawFluidField(const ISimulator& simulator, int drawTarget) {
    const auto& pressure = simulator.getPressure();
    const auto& density = simulator.getDensity();
    const auto& velocityX = simulator.getVelocityX();
    const auto& velocityY = simulator.getVelocityY();
    const auto& solid = simulator.getSolid();

    float cellSize = simulator.cellSize;
    int gridX = simulator.gridX;
    int gridY = simulator.gridY;

    // pressure range
    float minP = pressure[0];
    float maxP = pressure[0];
    for (int i = 0; i < gridX * gridY; i++) {
        minP = std::min(minP, pressure[i]);
        maxP = std::max(maxP, pressure[i]);
    }

    float maxAbsDiv = 0.0f;
    if (drawTarget == 4) {
        for (int i = 0; i < gridX; i++) {
            for (int j = 0; j < gridY; j++) {
                int idx = j * gridX + i;
                int idxRight = j * gridX + std::min(i + 1, gridX - 1);
                int idxTop = std::min(j + 1, gridY - 1) * gridX + i;
                int idxBottom = std::max(j - 1, 0) * gridX + i;
                float divergence = velocityX[idxRight] - velocityX[idx] + velocityY[idxTop] - velocityY[idxBottom];
                maxAbsDiv = std::max(maxAbsDiv, std::abs(divergence));
            }
        }
        maxAbsDiv = std::max(maxAbsDiv, 1e-4f);
    }

    // get ink references if needed
    bool inkInitialized = false;
    const std::vector<float>* inkRed_ptr = nullptr;
    const std::vector<float>* inkGreen_ptr = nullptr;
    const std::vector<float>* inkBlue_ptr = nullptr;
    if (drawTarget == 3 && simulator.inkInitialized) {
        inkRed_ptr = &simulator.getRedInk();
        inkGreen_ptr = &simulator.getGreenInk();
        inkBlue_ptr = &simulator.getBlueInk();
        inkInitialized = true;
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
                } else if (drawTarget == 4) {
                    int idxRight = j * gridX + std::min(i + 1, gridX - 1);
                    int idxTop = std::min(j + 1, gridY - 1) * gridX + i;
                    int idxBottom = std::max(j - 1, 0) * gridX + i;
                    float divergence = velocityX[idxRight] - velocityX[idx] + velocityY[idxTop] - velocityY[idxBottom];
                    mapDivergenceDebug(divergence, maxAbsDiv, r, g, b);
                } else if (drawTarget == 5) {
                    mapValueToHeatmap(density[idx], 0.0f, 1.0f, r, g, b);
                } else if (drawTarget == 6) {
                    int idxLeft = j * gridX + std::max(i - 1, 0);
                    int idxRight = j * gridX + std::min(i + 1, gridX - 1);
                    int idxTop = std::min(j + 1, gridY - 1) * gridX + i;
                    int idxBottom = std::max(j - 1, 0) * gridX + i;

                    float dx = density[idxRight] - density[idxLeft];
                    float dy = density[idxTop] - density[idxBottom];
                    float nz = 1.0f;
                    float nx = -dx * 4.0f;
                    float ny = -dy * 4.0f;
                    float normalLen = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (normalLen > 0.0f) {
                        nx /= normalLen;
                        ny /= normalLen;
                        nz /= normalLen;
                    }

                    const float lightX = 0.45f;
                    const float lightY = -0.55f;
                    const float lightZ = 0.70f;
                    float diffuse = std::max(0.0f, nx * lightX + ny * lightY + nz * lightZ);
                    float intensity = 0.20f + 0.80f * diffuse;
                    Uint8 shade = static_cast<Uint8>(std::max(0.0f, std::min(255.0f, intensity * 255.0f)));
                    r = g = b = shade;
                } else if (drawTarget == 7) {
                    auto smoothstep = [](float edge0, float edge1, float x) {
                        float t = (x - edge0) / (edge1 - edge0);
                        t = std::max(0.0f, std::min(1.0f, t));
                        return t * t * (3.0f - 2.0f * t);
                    };

                    const float threshold = 0.35f;
                    const float softness = 0.10f;

                    float centerDensity = density[idx];
                    float thresholdMask = smoothstep(threshold, threshold + softness, centerDensity);

                    float glowAccum = 0.0f;
                    int glowSamples = 0;
                    for (int oy = -1; oy <= 1; oy++) {
                        for (int ox = -1; ox <= 1; ox++) {
                            int sx = std::max(0, std::min(gridX - 1, i + ox));
                            int sy = std::max(0, std::min(gridY - 1, j + oy));
                            float d = density[sy * gridX + sx];
                            glowAccum += std::max(0.0f, d - threshold);
                            glowSamples++;
                        }
                    }

                    float glow = (glowSamples > 0) ? (glowAccum / static_cast<float>(glowSamples)) : 0.0f;
                    glow = std::min(1.0f, glow * 2.0f);

                    Uint8 baseR, baseG, baseB;
                    mapValueToHeatmap(centerDensity, 0.0f, 1.0f, baseR, baseG, baseB);

                    float outR = static_cast<float>(baseR) * (1.0f - 0.35f * thresholdMask) + 255.0f * glow;
                    float outG = static_cast<float>(baseG) * (1.0f - 0.35f * thresholdMask) + 190.0f * glow;
                    float outB = static_cast<float>(baseB) * (1.0f - 0.35f * thresholdMask) + 80.0f * glow;

                    r = static_cast<Uint8>(std::max(0.0f, std::min(255.0f, outR)));
                    g = static_cast<Uint8>(std::max(0.0f, std::min(255.0f, outG)));
                    b = static_cast<Uint8>(std::max(0.0f, std::min(255.0f, outB)));
                } else if (drawTarget == 3) {
                    // draw ink diffusion
                    if (inkInitialized && inkRed_ptr->size() > idx) {
                        mapInkToColor((*inkRed_ptr)[idx], (*inkGreen_ptr)[idx], (*inkBlue_ptr)[idx], r, g, b);
                    } else {
                        // default to white
                        r = 255; g = 255; b = 255;
                    }
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

void Renderer::drawVelocityField(const ISimulator& simulator, float velScale) {
    const auto& velocityX = simulator.getVelocityX();
    const auto& velocityY = simulator.getVelocityY();
    const auto& solid = simulator.getSolid();

    float cellSize = simulator.cellSize;
    int gridX = simulator.gridX;
    int gridY = simulator.gridY;

    float VELOCITY_VECTOR_LENGTH = 0.3f;

    // velocity vectors in white (normalized to unit length, then scaled)
    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            int idx = j * gridX + i;

            if (solid[idx] != 0.0f) {
                float vx = velocityX[idx];
                float vy = velocityY[idx];
                float magnitude = std::sqrt(vx * vx + vy * vy);
                
                if (magnitude > 0.001f) {
                    float normalizedLength = VELOCITY_VECTOR_LENGTH;
                    vx = (vx / magnitude) * normalizedLength;
                    vy = (vy / magnitude) * normalizedLength;
                }
                
                // horizontal vel component
                if (std::abs(vx) > 0.001f) {
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
                }

                // vertical vel component
                if (std::abs(vy) > 0.001f) {
                    int x0, y0;
                    convertCoordinates((i + 0.5f) * cellSize, j * cellSize, x0, y0);
                    int y1 = y0 - static_cast<int>(vy * velScale * canvasScale);

                    int steps = std::abs(y1 - y0);
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
}


// HISTOGRAM HELPERS
void Renderer::computeHistograms(const ISimulator& simulator) {
    IRenderer::HistogramData data;
    data.densityHistogramBins = densityHistogramBins;
    data.velocityHistogramBins = velocityHistogramBins;
    
    IRenderer::computeHistograms(simulator, data);
    
    densityHistogramMin = data.densityHistogramMin;
    densityHistogramMax = data.densityHistogramMax;
    velocityHistogramMin = data.velocityHistogramMin;
    velocityHistogramMax = data.velocityHistogramMax;
    densityHistogramBins = data.densityHistogramBins;
    velocityHistogramBins = data.velocityHistogramBins;
}

void Renderer::drawHistograms() {
    // Read histogram positions from global pixel layout
    auto dhIt = g_layoutPixels.components.find("density_histogram");
    auto vhIt = g_layoutPixels.components.find("velocity_histogram");
    if (dhIt == g_layoutPixels.components.end() || vhIt == g_layoutPixels.components.end()) return;

    const int dhistX = dhIt->second.x;
    const int dhistY = dhIt->second.y;
    const int histWidth = dhIt->second.width;
    const int histHeight = dhIt->second.height;
    const int vhistX = vhIt->second.x;
    const int vhistY = vhIt->second.y;
    const int vhistWidth = vhIt->second.width;
    const int vhistHeight = vhIt->second.height;
    
    int dmaxCount = 0;
    int vmaxCount = 0;
    for (int i = 0; i < IRenderer::HISTOGRAM_BINS; i++) {
        dmaxCount = std::max(dmaxCount, densityHistogramBins[i]);
        vmaxCount = std::max(vmaxCount, velocityHistogramBins[i]);
    }
    if (dmaxCount == 0 || vmaxCount == 0) return;
    
    // background
    Uint8 bg = 40;
    for (int y = dhistY; y < dhistY + histHeight; y++) {
        for (int x = dhistX; x < dhistX + histWidth; x++) {
            if (x >= 0 && x < windowWidth && y >= 0 && y < windowHeight) {
                setPixel(x, y, bg, bg, bg);
            }
        }
    }
    for (int y = vhistY; y < vhistY + vhistHeight; y++) {
        for (int x = vhistX; x < vhistX + vhistWidth; x++) {
            if (x >= 0 && x < windowWidth && y >= 0 && y < windowHeight) {
                setPixel(x, y, bg, bg, bg);
            }
        }
    }
    
    // border
    Uint8 border = 200;
    for (int x = dhistX; x < dhistX + histWidth; x++) {
        if (x >= 0 && x < windowWidth) {
            if (dhistY >= 0 && dhistY < windowHeight) {
                setPixel(x, dhistY, border, border, border); // top
            }
            if (dhistY + histHeight - 1 >= 0 && dhistY + histHeight - 1 < windowHeight) {
                setPixel(x, dhistY + histHeight - 1, border, border, border); // bottom
            }
        }
    }
    for (int y = dhistY; y < dhistY + histHeight; y++) {
        if (y >= 0 && y < windowHeight) {
            if (dhistX >= 0 && dhistX < windowWidth) {
                setPixel(dhistX, y, border, border, border); // left
            }
            if (dhistX + histWidth - 1 >= 0 && dhistX + histWidth - 1 < windowWidth) {
                setPixel(dhistX + histWidth - 1, y, border, border, border); // right
            }
        }
    }
    for (int x = vhistX; x < vhistX + vhistWidth; x++) {
        if (x >= 0 && x < windowWidth) {
            if (vhistY >= 0 && vhistY < windowHeight) {
                setPixel(x, vhistY, border, border, border); // top
            }
            if (vhistY + vhistHeight - 1 >= 0 && vhistY + vhistHeight - 1 < windowHeight) {
                setPixel(x, vhistY + vhistHeight - 1, border, border, border); // bottom
            }
        }
    }
    for (int y = vhistY; y < vhistY + vhistHeight; y++) {
        if (y >= 0 && y < windowHeight) {
            if (vhistX >= 0 && vhistX < windowWidth) {
                setPixel(vhistX, y, border, border, border); // left
            }
            if (vhistX + vhistWidth - 1 >= 0 && vhistX + vhistWidth - 1 < windowWidth) {
                setPixel(vhistX + vhistWidth - 1, y, border, border, border); // right
            }
        }
    }

    int barWidth = histWidth / IRenderer::HISTOGRAM_BINS;
    int vbarWidth = vhistWidth / IRenderer::HISTOGRAM_BINS;
    int padding = 1;
    
    // bars
    for (int i = 0; i < IRenderer::HISTOGRAM_BINS; i++) {
        int barHeight = static_cast<int>((static_cast<float>(densityHistogramBins[i]) / dmaxCount) * (histHeight - 20));
        int barX = dhistX + 10 + i * barWidth;
        float normalized = static_cast<float>(i) / IRenderer::HISTOGRAM_BINS;
        
        for (int x = barX; x < barX + barWidth - padding && x < dhistX + histWidth - 10; x++) {
            for (int y = dhistY + histHeight - 10; y >= dhistY + histHeight - 10 - barHeight && y >= dhistY + 10; y--) {
                if (x >= 0 && x < windowWidth && y >= 0 && y < windowHeight) {
                    Uint8 r, g, b;
                    mapValueToColor(normalized, 0.0f, 1.0f, r, g, b);
                    setPixel(x, y, r, g, b);
                }
            }
        }

        barHeight = static_cast<int>((static_cast<float>(velocityHistogramBins[i]) / vmaxCount) * (vhistHeight - 20));
        barX = vhistX + 10 + i * vbarWidth;

        for (int x = barX; x < barX + vbarWidth - padding && x < vhistX + vhistWidth - 10; x++) {
            for (int y = vhistY + vhistHeight - 10; y >= vhistY + vhistHeight - 10 - barHeight && y >= vhistY + 10; y--) {
                if (x >= 0 && x < windowWidth && y >= 0 && y < windowHeight) {
                    Uint8 r, g, b;
                    mapValueToVelocityColor(normalized, 0.0f, 1.0f, r, g, b);
                    setPixel(x, y, r, g, b);
                }
            }
        }
    }
}

