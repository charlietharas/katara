#include "sim.h"
#include <cmath>
#include <algorithm>
#include <omp.h>
#include <iostream>
#include <SDL2/SDL.h>

FluidSimulator::FluidSimulator(int resolution)
    : 
    // sim params
    resolution(resolution), // grid cells per world unit
    timeStep(1.0f / 60.0f),
    gravity(0.0f),
    density(1000.0f),
    overrelaxationCoefficient(1.9f), // speeds up projection
    gsIterations(40), // projection solver
    doVorticity(true),
    vorticity(10.0f),
    vorticityLen(5.0f),

    // wind tunnel state
    windTunnelStart(0.45f),
    windTunnelEnd(0.55f),
    windTunnelSide(0), // 0=left, 1=top, 2=bottom, 3=right, -1=disabled
    windTunnelVelocity(1.5f),

    // circle state
    circleX(0),
    circleY(0),
    prevCircleX(0),
    prevCircleY(0),
    circleVelX(0.0f),
    circleVelY(0.0f),
    circleRadius(0),

    // mouse state
    isDragging(false),

    // circle momentum transfer
    momentumTransferCoeff(0.25f),
    momentumTransferRadius(1.0f),

    // ink diffusion
    mixingRate(0.001f),
    diffusionRate(0.0001f),
    pressureStrength(0.1f),
    temporalWeightCurrent(0.95f),
    inkInitialized(false)
{
}

FluidSimulator::~FluidSimulator() {}

void FluidSimulator::init(
    const ImageData* imageData
) {
    bool imageLoaded = (imageData != nullptr && imageData->pixels != nullptr);

    if (imageLoaded) {
        float imageAspectRatio = static_cast<float>(imageData->width) / imageData->height;
        domainHeight = 1.0f;
        domainWidth = imageAspectRatio;
        domainSetByImage = true;

        if (imageAspectRatio > 1.0f) {
            resolution = static_cast<int>(resolution / imageAspectRatio);
        }
    } else {
        domainHeight = 1.0f;
        domainWidth = 1.5f;
    }
    cellHeight = domainHeight / resolution;
    halfCellHeight = cellHeight / 2.0f;

    gridX = static_cast<int>(domainWidth / cellHeight);
    gridY = static_cast<int>(domainHeight / cellHeight);
    xHeight = cellHeight * gridX;
    yHeight = cellHeight * gridY;

    pipeHeight = static_cast<int>(0.1f * gridY);
    pressureMultiplier = density * cellHeight / timeStep;

    int totalCells = gridX * gridY;

    // simulator fields
    x.resize(totalCells);
    y.resize(totalCells);
    s.resize(totalCells);
    p.resize(totalCells);
    d.resize(totalCells);
    newX.resize(totalCells);
    newY.resize(totalCells);
    newD.resize(totalCells);

    std::fill(s.begin(), s.end(), 1.0f);
    std::fill(d.begin(), d.end(), 1.0f);
    std::fill(p.begin(), p.end(), 0.0f);
    std::fill(x.begin(), x.end(), 0.0f);
    std::fill(y.begin(), y.end(), 0.0f);

    // ink diffusion fields
    if (imageLoaded) {
        r_ink.resize(totalCells);
        g_ink.resize(totalCells);
        b_ink.resize(totalCells);
        water.resize(totalCells);
        r_ink_prev.resize(totalCells);
        g_ink_prev.resize(totalCells);
        b_ink_prev.resize(totalCells);
        std::fill(r_ink.begin(), r_ink.end(), 0.0f);
        std::fill(g_ink.begin(), g_ink.end(), 0.0f);
        std::fill(b_ink.begin(), b_ink.end(), 0.0f);
        std::fill(water.begin(), water.end(), 1.0f);
        std::fill(r_ink_prev.begin(), r_ink_prev.end(), 0.0f);
        std::fill(g_ink_prev.begin(), g_ink_prev.end(), 0.0f);
        std::fill(b_ink_prev.begin(), b_ink_prev.end(), 0.0f);

        initializeFromImageData(imageData);
    }

    // initialize circle
    circleRadius = static_cast<int>(pipeHeight);
    circleX = gridX / 2;
    circleY = gridY / 2;

    // pre-calculate wind tunnel grid coordinates
    switch (windTunnelSide) {
        case 0: // left
        case 3: // right
            windTunnelStartCell = static_cast<int>(windTunnelStart * gridY);
            windTunnelEndCell = static_cast<int>(windTunnelEnd * gridY);
            windTunnelStartCell = std::max(0, std::min(gridY - 1, windTunnelStartCell));
            windTunnelEndCell = std::max(0, std::min(gridY - 1, windTunnelEndCell));
            break;
        case 1: // top
        case 2: // bottom
            windTunnelStartCell = static_cast<int>(windTunnelStart * gridX);
            windTunnelEndCell = static_cast<int>(windTunnelEnd * gridX);
            windTunnelStartCell = std::max(0, std::min(gridX - 1, windTunnelStartCell));
            windTunnelEndCell = std::max(0, std::min(gridX - 1, windTunnelEndCell));
            break;
        default:
            windTunnelStartCell = static_cast<int>(0.45f * gridY);
            windTunnelEndCell = static_cast<int>(0.55f * gridY);
    }

    // setup obstacles
    setupCircle();
    setupEdges();
}


void FluidSimulator::setupCircle() {
    for (int i = circleX - circleRadius; i < circleX + circleRadius; i++) {
        for (int j = circleY - circleRadius; j < circleY + circleRadius; j++) {
            if (i >= 0 && i < gridX && j >= 0 && j < gridY) {
                float dx = (i + 0.5f) - circleX;
                float dy = (j + 0.5f) - circleY;
                if (sqrt(dx * dx + dy * dy) <= circleRadius) {
                    s[idx(i, j)] = 0.0f;
                }
            }
        }
    }
}

void FluidSimulator::setupEdges() {
    int cx = gridX / 2;
    int cy = gridY / 2;

    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            // edge boundaries
            if (j == 0 || j == gridY-1 || i == 0 || i == gridX - 1) {
                s[idx(i, j)] = 0.0f;
            }

            // configurable wind tunnel
            // TODO configure direction by angle between 2 points; this is janky
            if (windTunnelSide != -1) {

                switch (windTunnelSide) {
                    case 0: // left
                        if (i == 1 && j >= windTunnelStartCell && j < windTunnelEndCell) {
                            x[idx(i, j)] = windTunnelVelocity;
                        }
                        if (i == 0 && j >= windTunnelStartCell && j < windTunnelEndCell) {
                            d[idx(i, j)] = 0.0f;
                        }
                        break;
                    case 1: // top
                        if (j == gridY-1 && i >= windTunnelStartCell && i < windTunnelEndCell) {
                            y[idx(i, j)] = -windTunnelVelocity;
                            d[idx(i, j)] = 0.0f;
                        }
                        break;
                    case 2: // bottom
                        if (j == 1 && i >= windTunnelStartCell && i < windTunnelEndCell) {
                            y[idx(i, j)] = windTunnelVelocity;
                        }
                        if (j == 0 && i >= windTunnelStartCell && i < windTunnelEndCell) {
                            d[idx(i, j)] = 0.0f;
                        }
                        break;
                    case 3: // right
                        if (i == gridX-1 && j >= windTunnelStartCell && j < windTunnelEndCell) {
                            x[idx(i, j)] = -windTunnelVelocity;
                            d[idx(i, j)] = 0.0f;
                        }
                        break;
                }
            }
        }
    }

    pipeHeight = windTunnelEndCell - windTunnelStartCell;
}

void FluidSimulator::initializeFromImageData(const ImageData* imageData) {
    if (!imageData || !imageData->pixels) return;
    Uint8* pixels = static_cast<Uint8*>(imageData->pixels);

    float DARKEST_BLACK = 0.05f; // minimum ink color; if it's 0 ink persists because it fucks up some multiplication somewhere
    float START_WATER = 0.05f;
    for (int j = 0; j < gridY; j++) {
        for (int i = 0; i < gridX; i++) {
            int cellIndex = idx(i, j);
            int imgX = (i * imageData->width) / gridX;
            int imgY = imageData->height - 1 - (j * imageData->height) / gridY; // image upside down

            if (imgX >= 0 && imgX < imageData->width && imgY >= 0 && imgY < imageData->height) {
                int pixelIndex = imgY * imageData->width + imgX;
                Uint8 r, g, b;
                if (imageData->bytesPerPixel == 4) {
                    r = pixels[pixelIndex * imageData->bytesPerPixel + imageData->rShift / 8];
                    g = pixels[pixelIndex * imageData->bytesPerPixel + imageData->gShift / 8];
                    b = pixels[pixelIndex * imageData->bytesPerPixel + imageData->bShift / 8];
                } else {
                    r = pixels[pixelIndex * imageData->bytesPerPixel];
                    g = pixels[pixelIndex * imageData->bytesPerPixel + 1];
                    b = pixels[pixelIndex * imageData->bytesPerPixel + 2];
                }

                // normalize
                r_ink[cellIndex] = std::max(0.05f, std::min(1.0f, r / 255.0f));
                g_ink[cellIndex] = std::max(0.05f, std::min(1.0f, g / 255.0f));
                b_ink[cellIndex] = std::max(0.05f, std::min(1.0f, b / 255.0f));
                water[cellIndex] = START_WATER;

                r_ink_prev[cellIndex] = r_ink[cellIndex];
                g_ink_prev[cellIndex] = g_ink[cellIndex];
                b_ink_prev[cellIndex] = b_ink[cellIndex];
            }
        }
    }

    inkInitialized = true;
}

void FluidSimulator::update() {
    integrate();
    project();
    extrapolate();
    advect();
    if (doVorticity) {
        applyVorticity();
    }
    smokeAdvect();

    if (inkInitialized) {
        inkUpdate();
    }
}

void FluidSimulator::integrate() {
    if (gravity == 0.0f) return;

    for (int i = 1; i < gridX; i++) {
        for (int j = 1; j < gridY; j++) {
            if (s[idx(i, j)] != 0.0f && s[idx(i, j-1)] != 0.0f) {
                y[idx(i, j)] += gravity * timeStep;
            }
        }
    }
}

void FluidSimulator::project() {
    // reset pressure field
    std::fill(p.begin(), p.end(), 0.0f);

    // Gauss-Seidel projection
    for (int n = 0; n < gsIterations; n++) {
        for (int i = 1; i < gridX - 1; i++) {
            for (int j = 1; j < gridY - 1; j++) {
                if (s[idx(i, j)] == 0.0f) continue;

                float sx0 = s[idx(i+1, j)];
                float sx1 = s[idx(i-1, j)];
                float sy0 = s[idx(i, j+1)];
                float sy1 = s[idx(i, j-1)];
                float b = sx0 + sx1 + sy0 + sy1;

                if (b == 0.0f) continue;

                float adjustedDivergence = -overrelaxationCoefficient * div(i, j) / b;

                x[idx(i+1, j)] += adjustedDivergence * sx0;
                x[idx(i, j)] -= adjustedDivergence * sx1;
                y[idx(i, j+1)] += adjustedDivergence * sy0;
                y[idx(i, j)] -= adjustedDivergence * sy1;
                p[idx(i, j)] += adjustedDivergence * pressureMultiplier;
            }
        }
    }
}

void FluidSimulator::extrapolate() {
    // set boundary tiles to copy neighbors
    for (int i = 0; i < gridX; i++) {
        x[idx(i, 0)] = x[idx(i, 1)];
        x[idx(i, gridY-1)] = x[idx(i, gridY-2)];
    }
    for (int j = 0; j < gridY; j++) {
        y[idx(0, j)] = y[idx(1, j)];
        y[idx(gridX-1, j)] = y[idx(gridX-2, j)];
    }
}

void FluidSimulator::advect() {
    newX = x;
    newY = y;

    #pragma omp parallel for
    for (int i = 1; i < gridX; i++) {
        for (int j = 1; j < gridY; j++) {
            if (s[idx(i, j)] != 0.0f) {
                // x vel advection
                if (s[idx(i-1, j)] != 0.0f && j < gridY-1) {
                    float x0 = i * cellHeight;
                    float y0 = j * cellHeight + halfCellHeight;
                    x0 -= x[idx(i, j)] * timeStep;
                    y0 -= neighborhoodY(i, j) * timeStep;
                    newX[idx(i, j)] = sample(x0, y0, 0);
                }

                // y vel advection
                if (s[idx(i, j-1)] != 0.0f && i < gridX-1) {
                    float x0 = i * cellHeight + halfCellHeight;
                    float y0 = j * cellHeight;
                    x0 -= neighborhoodX(i, j) * timeStep;
                    y0 -= y[idx(i, j)] * timeStep;
                    newY[idx(i, j)] = sample(x0, y0, 1);
                }
            }
        }
    }

    x = newX;
    y = newY;
}

void FluidSimulator::applyVorticity() {
    #pragma omp parallel for
    for (int i = 2; i < gridX - 2; i++) {
        for (int j = 2; j < gridY - 2; j++) {
            if (s[idx(i, j)] != 0.0f && s[idx(i-1, j)] != 0.0f &&
                s[idx(i+1, j)] != 0.0f && s[idx(i, j-1)] != 0.0f &&
                s[idx(i, j+1)] != 0.0f) {

                float dx = fabs(curl(i, j-1)) - fabs(curl(i, j+1));
                float dy = fabs(curl(i+1, j)) - fabs(curl(i-1, j));
                float len = sqrt(dx * dx + dy * dy) + vorticityLen;
                float c = curl(i, j);

                x[idx(i, j)] += timeStep * c * dx * vorticity / len;
                y[idx(i, j)] += timeStep * c * dy * vorticity / len;
            }
        }
    }
}

void FluidSimulator::smokeAdvect() {
    newD = d;

    #pragma omp parallel for
    for (int i = 1; i < gridX-1; i++) {
        for (int j = 1; j < gridY-1; j++) {
            if (s[idx(i, j)] != 0.0f) {
                float x0 = (x[idx(i, j)] + x[idx(i+1, j)]) / 2.0f;
                float y0 = (y[idx(i, j)] + y[idx(i, j+1)]) / 2.0f;
                float x1 = i * cellHeight + halfCellHeight - x0 * timeStep;
                float y1 = j * cellHeight + halfCellHeight - y0 * timeStep;
                newD[idx(i, j)] = sample(x1, y1, 2);
            }
        }
    }

    d = newD;
}

// ink stuff
void FluidSimulator::inkUpdate() {
    r_ink_prev = r_ink;
    g_ink_prev = g_ink;
    b_ink_prev = b_ink;

    inkAdvection();
    inkDiffusion();
    inkWaterMix();
    inkTemporalBlend();
}

void FluidSimulator::inkAdvection() {
    std::vector<float> new_r_ink = r_ink;
    std::vector<float> new_g_ink = g_ink;
    std::vector<float> new_b_ink = b_ink;

    #pragma omp parallel for
    for (int i = 1; i < gridX-1; i++) {
        for (int j = 1; j < gridY-1; j++) {
            if (shouldSkipInkCell(i, j)) continue;

            float vel_x = (x[idx(i, j)] + x[idx(i+1, j)]) / 2.0f;
            float vel_y = (y[idx(i, j)] + y[idx(i, j+1)]) / 2.0f;

            float x0 = i * cellHeight + halfCellHeight - vel_x * timeStep;
            float y0 = j * cellHeight + halfCellHeight - vel_y * timeStep;

            new_r_ink[idx(i, j)] = sample(x0, y0, 3);
            new_g_ink[idx(i, j)] = sample(x0, y0, 4);
            new_b_ink[idx(i, j)] = sample(x0, y0, 5);
        }
    }

    r_ink = new_r_ink;
    g_ink = new_g_ink;
    b_ink = new_b_ink;
}

void FluidSimulator::inkDiffusion() {
    std::vector<float> new_r_ink = r_ink;
    std::vector<float> new_g_ink = g_ink;
    std::vector<float> new_b_ink = b_ink;

    #pragma omp parallel for
    for (int i = 1; i < gridX-1; i++) {
        for (int j = 1; j < gridY-1; j++) {
            if (shouldSkipInkCell(i, j)) continue;

            int idx_ij = idx(i, j);

            float laplacian_r = (r_ink[idx(i+1, j
            )] + r_ink[idx(i-1, j)] +
                                r_ink[idx(i, j+1)] + r_ink[idx(i, j-1)] - 4.0f * r_ink[idx_ij]);
            float laplacian_g = (g_ink[idx(i+1, j)] + g_ink[idx(i-1, j)] +
                                g_ink[idx(i, j+1)] + g_ink[idx(i, j-1)] - 4.0f * g_ink[idx_ij]);
            float laplacian_b = (b_ink[idx(i+1, j)] + b_ink[idx(i-1, j)] +
                                b_ink[idx(i, j+1)] + b_ink[idx(i, j-1)] - 4.0f * b_ink[idx_ij]);

            new_r_ink[idx_ij] += diffusionRate * laplacian_r * timeStep;
            new_g_ink[idx_ij] += diffusionRate * laplacian_g * timeStep;
            new_b_ink[idx_ij] += diffusionRate * laplacian_b * timeStep;
        }
    }

    r_ink = new_r_ink;
    g_ink = new_g_ink;
    b_ink = new_b_ink;
}

void FluidSimulator::inkWaterMix() {
    float WATER_CAP = 0.2f;
    float MIXING_FACTOR = 0.1f;
    float REDUCTION_FACTOR = 0.05f;

    #pragma omp parallel for
    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            if (shouldSkipInkCell(i, j)) continue;

            int idx_ij = idx(i, j);

            float mixing = mixingRate * timeStep * MIXING_FACTOR;
            water[idx_ij] += (1.0f - water[idx_ij]) * mixing;
            water[idx_ij] = std::max(0.0f, std::min(WATER_CAP, water[idx_ij]));

            float ink_factor = 1.0f - water[idx_ij] * REDUCTION_FACTOR;
            r_ink[idx_ij] *= ink_factor;
            g_ink[idx_ij] *= ink_factor;
            b_ink[idx_ij] *= ink_factor;
        }
    }
}

void FluidSimulator::inkTemporalBlend() {
    #pragma omp parallel for
    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            if (shouldSkipInkCell(i, j, false)) continue;

            int idx_ij = idx(i, j);

            // temporal blending
            float weight_current = temporalWeightCurrent;
            float weight_prev = 1.0f - weight_current;

            r_ink[idx_ij] = weight_current * r_ink[idx_ij] + weight_prev * r_ink_prev[idx_ij];
            g_ink[idx_ij] = weight_current * g_ink[idx_ij] + weight_prev * g_ink_prev[idx_ij];
            b_ink[idx_ij] = weight_current * b_ink[idx_ij] + weight_prev * b_ink_prev[idx_ij];
        }
    }
}

// helpers
float FluidSimulator::div(int i, int j) {
    return x[idx(i+1, j)] - x[idx(i, j)] + y[idx(i, j+1)] - y[idx(i, j)];
}

float FluidSimulator::curl(int i, int j) {
    return x[idx(i, j+1)] - x[idx(i, j-1)] + y[idx(i-1, j)] - y[idx(i+1, j)];
}

float FluidSimulator::clamp(float n, float min, float max) {
    return std::min(max, std::max(min, n));
}

float FluidSimulator::neighborhoodX(int i, int j) {
    return (x[idx(i, j-1)] + x[idx(i, j)] + x[idx(i+1, j-1)] + x[idx(i+1, j)]) / 4.0f;
}

float FluidSimulator::neighborhoodY(int i, int j) {
    return (y[idx(i-1, j)] + y[idx(i, j)] + y[idx(i-1, j+1)] + y[idx(i, j+1)]) / 4.0f;
}

float FluidSimulator::sample(float i, float j, int type) {
    i = clamp(i, cellHeight, xHeight);
    j = clamp(j, cellHeight, yHeight);

    float xOffset = 0.0f;
    float yOffset = 0.0f;

    const std::vector<float>* field = nullptr;
    switch (type) {
        case 0:
            field = &x;
            yOffset = halfCellHeight;
            break;
        case 1:
            field = &y;
            xOffset = halfCellHeight;
            break;
        case 2:
            field = &d;
            xOffset = halfCellHeight;
            yOffset = halfCellHeight;
            break;
        case 3:
            field = &r_ink;
            xOffset = halfCellHeight;
            yOffset = halfCellHeight;
            break;
        case 4:
            field = &g_ink;
            xOffset = halfCellHeight;
            yOffset = halfCellHeight;
            break;
        case 5:
            field = &b_ink;
            xOffset = halfCellHeight;
            yOffset = halfCellHeight;
            break;
        default:
            return 0.0f;
    }

    int x0 = std::min(static_cast<int>(floor((i-xOffset) / cellHeight)), gridX-1);
    int x1 = std::min(x0+1, gridX-1);

    int y0 = std::min(static_cast<int>(floor((j-yOffset) / cellHeight)), gridY-1);
    int y1 = std::min(y0+1, gridY-1);

    float tx = ((i-xOffset) - x0*cellHeight) / cellHeight;
    float ty = ((j-yOffset) - y0*cellHeight) / cellHeight;

    float sx = 1.0f - tx;
    float sy = 1.0f - ty;

    float v = sx * sy * (*field)[idx(x0, y0)] +
            tx * sy * (*field)[idx(x1, y0)] +
            tx * ty * (*field)[idx(x1, y1)] +
            sx * ty * (*field)[idx(x0, y1)];

    return v;
}

bool FluidSimulator::isInsideCircle(int i, int j) {
    float dx = (i + 0.5f) - circleX;
    float dy = (j + 0.5f) - circleY;
    return sqrt(dx * dx + dy * dy) <= circleRadius;
}

void FluidSimulator::moveCircle(int newGridX, int newGridY) {
    prevCircleX = circleX;
    prevCircleY = circleY;

    float instantVelX = (newGridX - circleX) / timeStep;
    float instantVelY = (newGridY - circleY) / timeStep;

    // smoother circle velocity to reduce velocity jitter
    // (doesn't work that well D: )
    float alpha = 0.3f; // smoothing factor
    circleVelX = alpha * instantVelX + (1.0f - alpha) * circleVelX;
    circleVelY = alpha * instantVelY + (1.0f - alpha) * circleVelY;

    circleX = newGridX;
    circleY = newGridY;

    updateCircle(prevCircleX, prevCircleY, circleX, circleY);
}

void FluidSimulator::updateCircle(int prevX, int prevY, int newX, int newY) {
    updateCircleAreas(prevX, prevY, newX, newY);
    circleMomentumTransfer();
    setupEdges();
    enforceBoundaryConditions();
}

void FluidSimulator::enforceBoundaryConditions() {
    // clear velocity in all solid cells and their neighboring velocity components
    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            if (s[idx(i, j)] == 0.0f) {
                // clear velocity in the solid cell
                x[idx(i, j)] = 0.0f;
                y[idx(i, j)] = 0.0f;

                // clear velocity components that would move fluid into the solid
                if (i < gridX-1) x[idx(i+1, j)] = 0.0f;
                if (j < gridY-1) y[idx(i, j+1)] = 0.0f;
            }
        }
    }

    // preserve wind tunnel velocity
    if (windTunnelSide != -1) {

        switch (windTunnelSide) {
            case 0: // left
                for (int j = windTunnelStartCell; j < windTunnelEndCell; j++) {
                    if (j >= 0 && j < gridY) {
                        x[idx(1, j)] = windTunnelVelocity;
                    }
                }
                break;
            case 1: // top
                for (int i = windTunnelStartCell; i < windTunnelEndCell; i++) {
                    if (i >= 0 && i < gridX) {
                        y[idx(i, gridY-1)] = -windTunnelVelocity;
                    }
                }
                break;
            case 2: // bottom
                for (int i = windTunnelStartCell; i < windTunnelEndCell; i++) {
                    if (i >= 0 && i < gridX) {
                        y[idx(i, 1)] = windTunnelVelocity;
                    }
                }
                break;
            case 3: // right
                for (int j = windTunnelStartCell; j < windTunnelEndCell; j++) {
                    if (j >= 0 && j < gridY) {
                        x[idx(gridX-1, j)] = -windTunnelVelocity;
                    }
                }
                break;
        }
    }
}


void FluidSimulator::circleMomentumTransfer() {
    if (fabs(circleVelX) < 0.001f && fabs(circleVelY) < 0.001f) {
        return;
    }

    float effectiveRadius = circleRadius + momentumTransferRadius;

    // apply momentum to fluid cells near the ball surface
    for (int i = circleX - static_cast<int>(effectiveRadius) - 1;
         i <= circleX + static_cast<int>(effectiveRadius) + 1; i++) {
        for (int j = circleY - static_cast<int>(effectiveRadius) - 1;
             j <= circleY + static_cast<int>(effectiveRadius) + 1; j++) {

            if (i >= 0 && i < gridX && j >= 0 && j < gridY) {
                if (s[idx(i, j)] == 0.0f) continue;

                float dx = (i + 0.5f) - circleX;
                float dy = (j + 0.5f) - circleY;
                float distance = sqrt(dx * dx + dy * dy);

                // within influence radius but outside ball
                if (distance > circleRadius && distance <= effectiveRadius) {
                    // falloff is 1/r^2
                    float normalizedDistance = (distance - circleRadius) / momentumTransferRadius;
                    float falloff = 1.0f - normalizedDistance * normalizedDistance;
                    falloff = std::max(0.0f, falloff);

                    float densityFactor = d[idx(i, j)]; // weight velocity imparted by local density

                    float momentumX = circleVelX * momentumTransferCoeff * falloff * densityFactor;
                    float momentumY = circleVelY * momentumTransferCoeff * falloff * densityFactor;

                    x[idx(i, j)] += momentumX;
                    y[idx(i, j)] += momentumY;

                    // clamp velocities to prevent instability
                    float maxVel = 8.0f;
                    x[idx(i, j)] = std::max(-maxVel, std::min(maxVel, x[idx(i, j)]));
                    y[idx(i, j)] = std::max(-maxVel, std::min(maxVel, y[idx(i, j)]));
                }
            }
        }
    }
}

void FluidSimulator::updateCircleAreas(int prevX, int prevY, int newX, int newY) {
    // bounding box surrounding new and old circles
    int minI = std::min(prevX - circleRadius, newX - circleRadius);
    int maxI = std::max(prevX + circleRadius, newX + circleRadius);
    int minJ = std::min(prevY - circleRadius, newY - circleRadius);
    int maxJ = std::max(prevY + circleRadius, newY + circleRadius);

    for (int i = minI; i <= maxI; i++) {
        for (int j = minJ; j <= maxJ; j++) {
            if (i >= 0 && i < gridX && j >= 0 && j < gridY) {
                float dx = (i + 0.5f);
                float dy = (j + 0.5f);

                float prevDx = dx - prevX;
                float prevDy = dy - prevY;
                float distPrev = sqrt(prevDx * prevDx + prevDy * prevDy);

                float newDx = dx - newX;
                float newDy = dy - newY;
                float distNew = sqrt(newDx * newDx + newDy * newDy);

                bool wasInPrevCircle = distPrev <= circleRadius;
                bool isInNewCircle = distNew <= circleRadius;

                if (wasInPrevCircle && !isInNewCircle) {
                    s[idx(i, j)] = 1.0f; // make it fluid again
                    d[idx(i, j)] = 1.0f; // reset to default density
                    x[idx(i, j)] = 0.0f; // clear velocity
                    y[idx(i, j)] = 0.0f;
                } else if (!wasInPrevCircle && isInNewCircle) {
                    s[idx(i, j)] = 0.0f; // make it solid
                    // don't touch density -- this fixed the wisp !!!
                }
            }
        }
    }
}


void FluidSimulator::onMouseDrag(int gridX, int gridY) {
    if (isDragging) {
        // clamp circle to bounds
        int newX = std::max(circleRadius, std::min(gridX, this->gridX - circleRadius - 1));
        int newY = std::max(circleRadius, std::min(gridY, this->gridY - circleRadius - 1));

        if (newX != circleX || newY != circleY) {
            moveCircle(newX, newY);
        }
    }
}

void FluidSimulator::onMouseDown(int gridX, int gridY) {
    isDragging = true;
}

void FluidSimulator::onMouseUp() {
    isDragging = false;
}

bool FluidSimulator::shouldSkipInkCell(int i, int j, bool checkNoInk) const {
    // skip solid cells
    if (s[idx(i, j)] == 0.0f) return true;

    // skip wind tunnels
    int cy = gridY / 2;
    if (i == 1 && j >= cy - pipeHeight / 2 && j < cy + pipeHeight / 2) {
        return true;
    }

    // skip cells with no ink
    if (checkNoInk) {
        int idx_ij = idx(i, j);
        if (r_ink[idx_ij] == 0.0f && g_ink[idx_ij] == 0.0f && b_ink[idx_ij] == 0.0f) {
            return true;
        }
    }

    return false;
}
