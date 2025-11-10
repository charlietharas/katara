#include "sim.h"
#include <cmath>
#include <algorithm>
#include <omp.h>
#include <iostream>

FluidSimulator::FluidSimulator()
    : resolution(100), timeStep(1.0f/60.0f), gravity(0.0f), density(1000.0f),
      overrelaxationCoefficient(1.9f), gsIterations(40), doVorticity(true),
      vorticity(10.0f), vorticityLen(5.0f), windTunnelVel(1.5f),
      circleX(0), circleY(0), prevCircleX(0), prevCircleY(0), circleVelX(0.0f), circleVelY(0.0f),
      circleRadius(0), isDragging(false), momentumTransferCoeff(0.25f), momentumTransferRadius(1.0f) {
}

FluidSimulator::~FluidSimulator() {}

void FluidSimulator::init() {
    domainHeight = 1.0f;
    domainWidth = 1.5f;
    cellHeight = domainHeight / resolution;
    halfCellHeight = cellHeight / 2.0f;

    gridX = static_cast<int>(domainWidth / cellHeight);
    gridY = static_cast<int>(domainHeight / cellHeight);
    xHeight = cellHeight * gridX;
    yHeight = cellHeight * gridY;

    pipeHeight = static_cast<int>(0.1f * gridY);
    pressureMultiplier = density * cellHeight / timeStep;

    int totalCells = gridX * gridY;
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

    // initialize circle
    circleRadius = static_cast<int>(pipeHeight * 1.5f);
    circleX = gridX / 2;
    circleY = gridY / 2;

    setupObstacles();
    setupBoundariesAndWindTunnel();
}


void FluidSimulator::setupObstacles() {
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


void FluidSimulator::update() {
    integrate();
    project();
    extrapolate();
    advect();
    if (doVorticity) {
        applyVorticity();
    }
    advectSmoke();
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

void FluidSimulator::advectSmoke() {
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
    if (type == 0) { // advect x
        field = &x;
        yOffset = halfCellHeight;
    } else if (type == 1) { // advect y
        field = &y;
        xOffset = halfCellHeight;
    } else if (type == 2) { // advect smoke
        field = &d;
        xOffset = halfCellHeight;
        yOffset = halfCellHeight;
    } else {
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

void FluidSimulator::reset() {
    init();
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
    float alpha = 0.3f;
    circleVelX = alpha * instantVelX + (1.0f - alpha) * circleVelX;
    circleVelY = alpha * instantVelY + (1.0f - alpha) * circleVelY;

    circleX = newGridX;
    circleY = newGridY;

    updateSolidFieldForCircle(prevCircleX, prevCircleY, circleX, circleY);
}

void FluidSimulator::updateSolidFieldForCircle(int prevX, int prevY, int newX, int newY) {
    updateCircleAreas(prevX, prevY, newX, newY);

    // push fluid around
    transferMomentumToFluid();

    // setup boundaries and wind tunnel
    setupBoundariesAndWindTunnel();

    // enforce boundary conditions after setup
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

    // preserve wind tunnel velocity (JANKY!!)
    int cy = gridY / 2;
    for (int j = cy - pipeHeight / 2; j < cy + pipeHeight / 2; j++) {
        if (j >= 0 && j < gridY) {
            x[idx(1, j)] = windTunnelVel;
        }
    }
}


void FluidSimulator::transferMomentumToFluid() {
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

void FluidSimulator::setupBoundariesAndWindTunnel() {
    int cx = gridX / 2;
    int cy = gridY / 2;

    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            // edge boundaries
            if (j == 0 || j == gridY-1 || i == 0) {
                s[idx(i, j)] = 0.0f;
            }

            // wind tunnel
            if (i == 1 && j >= cy - pipeHeight / 2 && j < cy + pipeHeight / 2) {
                x[idx(i, j)] = windTunnelVel;
            }
            if (i == 0 && j >= cy - pipeHeight / 2 && j < cy + pipeHeight / 2) {
                d[idx(i, j)] = 0.0f;
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