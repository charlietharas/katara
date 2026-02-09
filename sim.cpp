#include "sim.h"
#include <cmath>
#include <algorithm>
#include <omp.h>
#include <iostream>
#include <SDL2/SDL.h>

Simulator::Simulator(const Config& config)
    : resolution(config.simulation.resolution),
      timestep(config.simulation.timestep),
      density(config.simulation.fluidDensity),
      overrelaxationCoefficient(config.simulation.projection.overrelaxationCoefficient),
      projectionIters(config.simulation.projection.iterations),
      doVorticity(config.simulation.vorticity.enabled),
      vorticity(config.simulation.vorticity.strength),
      vorticityLen(config.simulation.vorticity.lengthScale),
      momentumTransferStrength(config.simulation.circle.momentumTransferStrength),
      momentumTransferRadius(config.simulation.circle.momentumTransferRadius)
{
    windTunnelSide = config.simulation.windTunnel.side;
    windTunnelSpeed = config.simulation.windTunnel.velocity;
    gravity = config.simulation.gravity;
}

Simulator::~Simulator() {}


// MAIN SIM LOOP
bool Simulator::init(const Config& config, const ImageData* imageData, float aspectRatio) {
    this->config = &config;
    bool imageLoaded = imageData != nullptr && imageData->pixels != nullptr;

    // set domain dimensions based on aspect ratio
    // whichever dimension is smaller is set to 1.0f
    if (aspectRatio >= 1.0f) { // landscape
        domainHeight = 1.0f; // smaller
        domainWidth = aspectRatio;
    } else { // portrait
        domainHeight = 1.0f / aspectRatio;
        domainWidth = 1.0f; // smaller
    }

    cellSize = (aspectRatio >= 1.0f ? domainHeight : domainWidth) / resolution;
    halfCellSize = cellSize / 2.0f;

    gridX = static_cast<int>(domainWidth / cellSize);
    gridY = static_cast<int>(domainHeight / cellSize);
    int totalCells = gridX * gridY;
    xHeight = cellSize * gridX;
    yHeight = cellSize * gridY;

    // convert circle radius from world units to grid units
    circleRadius = static_cast<int>(config.simulation.circle.radius / cellSize);
    
    // simulator fields
    x.resize(totalCells);
    y.resize(totalCells);
    s.resize(totalCells);
    p.resize(totalCells);
    d.resize(totalCells);
    newX.resize(totalCells);
    newY.resize(totalCells);
    newD.resize(totalCells);
    std::fill(x.begin(), x.end(), 0.0f);
    std::fill(y.begin(), y.end(), 0.0f);
    std::fill(s.begin(), s.end(), 1.0f);
    std::fill(d.begin(), d.end(), 1.0f);
    std::fill(p.begin(), p.end(), 0.0f);

    // ink diffusion fields
    if (imageLoaded) {
        inkRed.resize(totalCells);
        inkGreen.resize(totalCells);
        inkBlue.resize(totalCells);
        newInkRed.resize(totalCells);
        newInkGreen.resize(totalCells);
        newInkBlue.resize(totalCells);
        std::fill(inkRed.begin(), inkRed.end(), 0.0f);
        std::fill(inkGreen.begin(), inkGreen.end(), 0.0f);
        std::fill(inkBlue.begin(), inkBlue.end(), 0.0f);

        initializeFromImageData(config, imageData);
    }

    // setup circle
    circleX = gridX / 2;
    circleY = gridY / 2;
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
    
    // setup edges
    int cx = gridX / 2;
    int cy = gridY / 2;
    int edgesMask = config.simulation.edges;
    bool leftEdge = edgesMask & 8;
    bool topEdge = edgesMask & 4;
    bool bottomEdge = edgesMask & 2;
    bool rightEdge = edgesMask & 1;
    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            if ((i == 0 && leftEdge) ||
                (i == gridX - 1 && rightEdge) ||
                (j == 0 && bottomEdge) ||
                (j == gridY - 1 && topEdge)) {
                s[idx(i, j)] = 0.0f;
            }
        }
    }

    // setup wind tunnel
    // calculate wind tunnel grid coordinates
    float windTunnelStart = config.simulation.windTunnel.startPosition;
    float windTunnelEnd = config.simulation.windTunnel.endPosition;
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
    
    pipeHeight = windTunnelEndCell - windTunnelStartCell;
    if (windTunnelSide != -1) {
        switch (windTunnelSide) {
            case 0: // left
                for (int j = windTunnelStartCell; j < windTunnelEndCell; j++) {
                    x[idx(1, j)] = windTunnelSpeed;
                    d[idx(0, j)] = 0.0f;
                }
                break;
            case 1: // top
                for (int i = windTunnelStartCell; i < windTunnelEndCell; i++) {
                    y[idx(i, gridY-2)] = -windTunnelSpeed;
                    d[idx(i, gridY-1)] = 0.0f;
                }
                break;
            case 2: // bottom
                for (int i = windTunnelStartCell; i < windTunnelEndCell; i++) {
                    y[idx(i, 1)] = windTunnelSpeed;
                    d[idx(i, 0)] = 0.0f;
                }
                break;
            case 3: // right
                for (int j = windTunnelStartCell; j < windTunnelEndCell; j++) {
                    x[idx(gridX-1, j)] = -windTunnelSpeed;
                    d[idx(gridX-1, j)] = 0.0f;
                }
                break;
        }
    }

    return true;
}

void Simulator::initializeFromImageData(const Config& config, const ImageData* imageData) {
    if (!imageData || !imageData->pixels) return;
    Uint8* pixels = static_cast<Uint8*>(imageData->pixels);

    float DARKEST_BLACK = 0.02f; // minimum ink color; if it's 0 ink persists because it fucks up some multiplication somewhere
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
                inkRed[cellIndex] = std::max(DARKEST_BLACK, std::min(1.0f, r / 255.0f));
                inkGreen[cellIndex] = std::max(DARKEST_BLACK, std::min(1.0f, g / 255.0f));
                inkBlue[cellIndex] = std::max(DARKEST_BLACK, std::min(1.0f, b / 255.0f));
            }
        }
    }

    inkInitialized = true;
}

void Simulator::update() {
    // base steps
    if (gravity != 0.0f) { 
        integrate();
    }
    project();
    extrapolate();
    advect();
    if (doVorticity) {
        applyVorticity();
    }
}

void Simulator::integrate() {
    for (int i = 1; i < gridX; i++) {
        for (int j = 1; j < gridY; j++) {
            if (s[idx(i, j)] != 0.0f && s[idx(i, j-1)] != 0.0f) {
                y[idx(i, j)] += gravity * timestep;
            }
        }
    }
}

void Simulator::project() {
    // reset pressure field
    std::fill(p.begin(), p.end(), 0.0f);

    // Gauss-Seidel projection
    for (int n = 0; n < projectionIters; n++) {
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
                p[idx(i, j)] += adjustedDivergence;
            }
        }
    }
}

void Simulator::extrapolate() {
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

void Simulator::advect() {
    newX = x;
    newY = y;
    newD = d;
    if (inkInitialized) {
        newInkRed = inkRed;
        newInkGreen = inkGreen;
        newInkBlue = inkBlue;
    }
    int cy = gridY / 2;

    #pragma omp parallel for
    for (int i = 1; i < gridX; i++) {
        for (int j = 1; j < gridY; j++) {
            if (s[idx(i, j)] != 0.0f) {
                // x vel advection
                if (s[idx(i-1, j)] != 0.0f && j < gridY-1) {
                    float x0 = i * cellSize;
                    float y0 = j * cellSize + halfCellSize;
                    x0 -= x[idx(i, j)] * timestep;
                    y0 -= neighborhoodY(i, j) * timestep;
                    newX[idx(i, j)] = sample(x0, y0, 0);
                }

                // y vel advection
                if (s[idx(i, j-1)] != 0.0f && i < gridX-1) {
                    float x0 = i * cellSize + halfCellSize;
                    float y0 = j * cellSize;
                    x0 -= neighborhoodX(i, j) * timestep;
                    y0 -= y[idx(i, j)] * timestep;
                    newY[idx(i, j)] = sample(x0, y0, 1);
                }

                // smoke advection
                float x0 = (x[idx(i, j)] + x[idx(i+1, j)]) / 2.0f;
                float y0 = (y[idx(i, j)] + y[idx(i, j+1)]) / 2.0f;
                float x1 = i * cellSize + halfCellSize - x0 * timestep;
                float y1 = j * cellSize + halfCellSize - y0 * timestep;
                newD[idx(i, j)] = sample(x1, y1, 2);

                // ink advection
                if (inkInitialized && s[idx(i, j)] != 0.0f) {
                    float vel_x = (x[idx(i, j)] + x[idx(i+1, j)]) / 2.0f;
                    float vel_y = (y[idx(i, j)] + y[idx(i, j+1)]) / 2.0f;
        
                    float x0 = i * cellSize + halfCellSize - vel_x * timestep;
                    float y0 = j * cellSize + halfCellSize - vel_y * timestep;
        
                    newInkRed[idx(i, j)] = sample(x0, y0, 3);
                    newInkGreen[idx(i, j)] = sample(x0, y0, 4);
                    newInkBlue[idx(i, j)] = sample(x0, y0, 5);
                }
            }
        }
    }

    x = newX;
    y = newY;
    d = newD;
    if (inkInitialized) {
        inkRed = newInkRed;
        inkGreen = newInkGreen;
        inkBlue = newInkBlue;
    }
}

void Simulator::applyVorticity() {
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

                x[idx(i, j)] += timestep * c * dx * vorticity / len;
                y[idx(i, j)] += timestep * c * dy * vorticity / len;
            }
        }
    }
}

// FIELD HELPERS
float Simulator::div(int i, int j) {
    return x[idx(i+1, j)] - x[idx(i, j)] + y[idx(i, j+1)] - y[idx(i, j)];
}

float Simulator::curl(int i, int j) {
    return x[idx(i, j+1)] - x[idx(i, j-1)] + y[idx(i-1, j)] - y[idx(i+1, j)];
}

float Simulator::clamp(float n, float min, float max) {
    return std::min(max, std::max(min, n));
}

float Simulator::neighborhoodX(int i, int j) {
    return (x[idx(i, j-1)] + x[idx(i, j)] + x[idx(i+1, j-1)] + x[idx(i+1, j)]) / 4.0f;
}

float Simulator::neighborhoodY(int i, int j) {
    return (y[idx(i-1, j)] + y[idx(i, j)] + y[idx(i-1, j+1)] + y[idx(i, j+1)]) / 4.0f;
}

float Simulator::sample(float i, float j, int type) {
    i = clamp(i, cellSize, xHeight);
    j = clamp(j, cellSize, yHeight);

    float xOffset = 0.0f;
    float yOffset = 0.0f;

    const std::vector<float>* field = nullptr;
    switch (type) {
        case 0:
            field = &x;
            yOffset = halfCellSize;
            break;
        case 1:
            field = &y;
            xOffset = halfCellSize;
            break;
        case 2:
            field = &d;
            xOffset = halfCellSize;
            yOffset = halfCellSize;
            break;
        case 3:
            field = &inkRed;
            xOffset = halfCellSize;
            yOffset = halfCellSize;
            break;
        case 4:
            field = &inkGreen;
            xOffset = halfCellSize;
            yOffset = halfCellSize;
            break;
        case 5:
            field = &inkBlue;
            xOffset = halfCellSize;
            yOffset = halfCellSize;
            break;
        default:
            return 0.0f;
    }

    int x0 = std::min(static_cast<int>(floor((i-xOffset) / cellSize)), gridX-1);
    int x1 = std::min(x0+1, gridX-1);

    int y0 = std::min(static_cast<int>(floor((j-yOffset) / cellSize)), gridY-1);
    int y1 = std::min(y0+1, gridY-1);

    float tx = ((i-xOffset) - x0*cellSize) / cellSize;
    float ty = ((j-yOffset) - y0*cellSize) / cellSize;

    float sx = 1.0f - tx;
    float sy = 1.0f - ty;

    float v = sx * sy * (*field)[idx(x0, y0)] +
            tx * sy * (*field)[idx(x1, y0)] +
            tx * ty * (*field)[idx(x1, y1)] +
            sx * ty * (*field)[idx(x0, y1)];

    return v;
}


// CIRCLE HELPERS
void Simulator::moveCircle(int newGridX, int newGridY) {
    prevCircleX = circleX;
    prevCircleY = circleY;

    float instantVelX = (newGridX - circleX) / timestep;
    float instantVelY = (newGridY - circleY) / timestep;

    // smoother circle velocity to reduce velocity jitter
    // (doesn't work that well D: )
    float alpha = 0.3f; // smoothing factor
    circleVelX = alpha * instantVelX + (1.0f - alpha) * circleVelX;
    circleVelY = alpha * instantVelY + (1.0f - alpha) * circleVelY;

    circleX = newGridX;
    circleY = newGridY;

    updateCircle(prevCircleX, prevCircleY, circleX, circleY);
}

void Simulator::updateCircle(int prevX, int prevY, int newX, int newY) {
    updateCircleAreas(prevX, prevY, newX, newY);
    circleMomentumTransfer();
    enforceBoundaryConditions();
}

void Simulator::enforceBoundaryConditions() {
    // clear velocity in all solid cells and their neighboring velocity components
    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            if (s[idx(i, j)] == 0.0f) {
                // clear velocity in the solid cell
                x[idx(i, j)] = 0.0f;
                y[idx(i, j)] = 0.0f;

                // this is weird and hacky on both the cpu and gpu versions
                // here we clear only the bottom and right cells 
                // but only if this cell is solid
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
                        x[idx(1, j)] = windTunnelSpeed;
                        x[idx(0, j)] = windTunnelSpeed;
                        d[idx(0, j)] = 0.0f;
                    }
                }
                break;
            case 1: // top
                for (int i = windTunnelStartCell; i < windTunnelEndCell; i++) {
                    if (i >= 0 && i < gridX) {
                        y[idx(i, gridY-2)] = -windTunnelSpeed;
                        y[idx(i, gridY-1)] = -windTunnelSpeed;
                        d[idx(i, gridY-1)] = 0.0f;
                    }
                }
                break;
            case 2: // bottom
                for (int i = windTunnelStartCell; i < windTunnelEndCell; i++) {
                    if (i >= 0 && i < gridX) {
                        y[idx(i, 1)] = windTunnelSpeed;
                        y[idx(i, 0)] = windTunnelSpeed;
                        d[idx(i, 0)] = 0.0f;
                    }
                }
                break;
            case 3: // right
                for (int j = windTunnelStartCell; j < windTunnelEndCell; j++) {
                    if (j >= 0 && j < gridY) {
                        x[idx(gridX-2, j)] = -windTunnelSpeed;
                        x[idx(gridX-1, j)] = -windTunnelSpeed;
                        d[idx(gridX-1, j)] = 0.0f;
                    }
                }
                break;
        }
    }
}

void Simulator::circleMomentumTransfer() {
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

                    float momentumX = circleVelX * momentumTransferStrength * falloff * densityFactor;
                    float momentumY = circleVelY * momentumTransferStrength * falloff * densityFactor;

                    x[idx(i, j)] += momentumX;
                    y[idx(i, j)] += momentumY;

                    // NOTE: max velocity clamping for force imparted by the circle, for stability
                    float maxVel = 8.0f;
                    x[idx(i, j)] = std::max(-maxVel, std::min(maxVel, x[idx(i, j)]));
                    y[idx(i, j)] = std::max(-maxVel, std::min(maxVel, y[idx(i, j)]));
                }
            }
        }
    }
}

void Simulator::updateCircleAreas(int prevX, int prevY, int newX, int newY) {
    // bounding box surrounding new and old circles
    int minI = std::min(prevX - circleRadius, newX - circleRadius);
    int maxI = std::max(prevX + circleRadius, newX + circleRadius);
    int minJ = std::min(prevY - circleRadius, newY - circleRadius);
    int maxJ = std::max(prevY + circleRadius, newY + circleRadius);

    for (int i = minI; i <= maxI; i++) {
        for (int j = minJ; j <= maxJ; j++) {
            if (i > 0 && i < gridX-1 && j > 0 && j < gridY-1) {
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
