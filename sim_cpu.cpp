#include "sim_cpu.h"
#include <cmath>
#include <algorithm>
#ifndef __EMSCRIPTEN__
#include <omp.h>
#endif
#include <iostream>
#include <SDL2/SDL.h>

Simulator::Simulator(const Config& config)
    : resolution(config.simulation.resolution),
      timestep(config.simulation.timestep),
      overrelaxationCoefficient(config.simulation.projection.overrelaxationCoefficient),
      projectionIters(config.simulation.projection.iterations),
      doVorticity(config.simulation.vorticity.strength > 0.0f),
      vorticity(config.simulation.vorticity.strength),
      vorticityLen(config.simulation.vorticity.lengthScale),
      momentumTransferStrength(config.simulation.circle.momentumTransferStrength),
      momentumTransferRadius(config.simulation.circle.momentumTransferRadius)
{
    windTunnelSide = config.simulation.windTunnel.side;
    windTunnelSpeed = config.simulation.windTunnel.velocity;
}

Simulator::~Simulator() {}

void ISimulator::recomputeWindTunnelCells(const Config& config) {
    const int side = config.simulation.windTunnel.side;
    windTunnelStart = config.simulation.windTunnel.startPosition;
    windTunnelEnd = config.simulation.windTunnel.endPosition;
    switch (side) {
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
}


bool Simulator::gridConfigChanged(const Config& config) const {
    return config.simulation.resolution != resolution ||
           config.simulation.edges != edgesMask;
}

bool Simulator::rebuildGridFromConfig(const Config& config) {
    resolution = config.simulation.resolution;
    edgesMask = config.simulation.edges;

    const float aspectRatio = domainWidth / domainHeight;
    cellSize = (aspectRatio >= 1.0f ? domainHeight : domainWidth) / resolution;
    halfCellSize = cellSize / 2.0f;

    gridX = static_cast<int>(domainWidth / cellSize);
    gridY = static_cast<int>(domainHeight / cellSize);
    const int totalCells = gridX * gridY;
    xHeight = cellSize * gridX;
    yHeight = cellSize * gridY;

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
    ink.resize(totalCells * 4);
    newInk.resize(totalCells * 4);
    std::fill(ink.begin(), ink.end(), 0.0f);
    std::fill(newInk.begin(), newInk.end(), 0.0f);
    inkInitialized = false;

    mouseCircleRadius = static_cast<int>(config.simulation.circle.radius / cellSize);
    baseCircleRadius = mouseCircleRadius;

    if (isMouseInput(g_config.inputMode)) {
        const int r = std::max(1, mouseCircleRadius);
        mouseCircleX = std::max(r, std::min(mouseCircleX > 0 ? mouseCircleX : gridX / 2, gridX - r - 1));
        mouseCircleY = std::max(r, std::min(mouseCircleY > 0 ? mouseCircleY : gridY / 2, gridY - r - 1));
        mousePrevCircleX = mouseCircleX;
        mousePrevCircleY = mouseCircleY;
        mouseCircleVelX = 0.0f;
        mouseCircleVelY = 0.0f;
        isMouseDragging = false;
    }

    numCircles = 0;
    for (int i = 0; i < HandTracking::MAX_CIRCLES; i++) {
        circles[i].present = false;
        circles[i].wasPresent = false;
        circles[i].x = 0;
        circles[i].y = 0;
        circles[i].prevX = 0;
        circles[i].prevY = 0;
        circles[i].velX = 0.0f;
        circles[i].velY = 0.0f;
        circles[i].z = 0.0f;
        circles[i].scaledRadius = baseCircleRadius;
    }
    for (int i = 0; i < HandTracking::MAX_SEGMENTS; i++) {
        segments[i].present = false;
        segments[i].wasPresent = false;
        segments[i].startX = 0;
        segments[i].startY = 0;
        segments[i].endX = 0;
        segments[i].endY = 0;
        segments[i].startRadius = 0.0f;
        segments[i].endRadius = 0.0f;
    }
    numSegments = 0;
    numPresentSegments = 0;

    const bool leftEdge = edgesMask & 8;
    const bool topEdge = edgesMask & 4;
    const bool bottomEdge = edgesMask & 2;
    const bool rightEdge = edgesMask & 1;
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

    windTunnelSide = config.simulation.windTunnel.side;
    windTunnelSpeed = config.simulation.windTunnel.velocity;
    recomputeWindTunnelCells(config);
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
                    y[idx(i, gridY - 1)] = -windTunnelSpeed;
                    d[idx(i, gridY - 1)] = 0.0f;
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
                    x[idx(gridX - 1, j)] = -windTunnelSpeed;
                    d[idx(gridX - 1, j)] = 0.0f;
                }
                break;
        }
    }

    this->config = &g_config;
    return true;
}

// MAIN SIM LOOP
bool Simulator::init(const Config& config, const ImageData* imageData, float aspectRatio) {
    this->config = &g_config;
    const bool imageLoaded = imageData != nullptr && imageData->pixels != nullptr;

    if (aspectRatio >= 1.0f) { // landscape
        domainHeight = 1.0f;
        domainWidth = aspectRatio;
    } else { // portrait
        domainHeight = 1.0f / aspectRatio;
        domainWidth = 1.0f;
    }

    if (!rebuildGridFromConfig(config)) {
        return false;
    }

    if (imageLoaded) {
        initializeFromImageData(config, imageData);
    }

    return true;
}

void Simulator::initializeFromImageData(const Config& config, const ImageData* imageData) {
    if (!imageData || !imageData->pixels) return;
    const int inkFloats = gridX * gridY * 4;
    if (static_cast<int>(ink.size()) != inkFloats) {
        ink.assign(inkFloats, 0.0f);
    }
    if (static_cast<int>(newInk.size()) != inkFloats) {
        newInk.assign(inkFloats, 0.0f);
    }
    Uint8* pixels = static_cast<Uint8*>(imageData->pixels);

    float DARKEST_BLACK = 0.02f; // minimum ink color; if it's 0 ink persists because it fucks up some multiplication somewhere
    for (int j = 0; j < gridY; j++) {
        for (int i = 0; i < gridX; i++) {
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

                int base = inkCellBase(i, j);
                ink[base] = std::max(DARKEST_BLACK, std::min(1.0f, r / 255.0f));
                ink[base + 1] = std::max(DARKEST_BLACK, std::min(1.0f, g / 255.0f));
                ink[base + 2] = std::max(DARKEST_BLACK, std::min(1.0f, b / 255.0f));
                ink[base + 3] = 1.0f;
            }
        }
    }

    inkInitialized = true;
}

// NOTE CPU fingertip input is known to be janky
// oh well!
int Simulator::scaleRadiusByZ(float z) {
    return ::scaleRadiusByZ(z, baseCircleRadius, config->simulation.circle);
}

void Simulator::updateCircles(const FingertipData* fingertips, int count) {
    int actualCount = std::min(count, HandTracking::MAX_CIRCLES);
    numCircles = actualCount;

    for (int i = 0; i < actualCount; i++) {
        CircleState& circle = circles[i];
        circle.prevX = circle.x;
        circle.prevY = circle.y;

        if (fingertips[i].present <= 0.5f) {
            circle.present = false;
            circle.x = 0;
            circle.y = 0;
            circle.smoothedX = 0.0f;
            circle.smoothedY = 0.0f;
            circle.velX = 0.0f;
            circle.velY = 0.0f;
        } else {
            int newGridX = static_cast<int>((1.0f - fingertips[i].x) * gridX);
            int newGridY = static_cast<int>((1.0f - fingertips[i].y) * gridY);
            newGridX = std::max(baseCircleRadius, std::min(newGridX, gridX - baseCircleRadius - 1));
            newGridY = std::max(baseCircleRadius, std::min(newGridY, gridY - baseCircleRadius - 1));

            float handSpeed = 0.0f;
            if (circle.wasPresent) {
                float dx = static_cast<float>(newGridX) - circle.smoothedX;
                float dy = static_cast<float>(newGridY) - circle.smoothedY;
                handSpeed = std::sqrt(dx * dx + dy * dy) / timestep;
            }

            applyHandSmoothing(newGridX, newGridY, circle.smoothedX, circle.smoothedY,
                               circle.x, circle.y, circle.wasPresent,
                               config->simulation.circle, timestep);

            circle.z = fingertips[i].z;
            circle.scaledRadius = scaleRadiusByZ(fingertips[i].z);
            circle.present = true;
            circle.wasPresent = true;

            float instantVelX = (circle.x - circle.prevX) / timestep;
            float instantVelY = (circle.y - circle.prevY) / timestep;
            applyCircleVelocitySmoothing(instantVelX, instantVelY, circle.velX, circle.velY,
                                         handSpeed, config->simulation.circle);
        }
    }

    for (int i = actualCount; i < HandTracking::MAX_CIRCLES; i++) {
        circles[i].present = false;
        circles[i].wasPresent = false;
        circles[i].x = 0;
        circles[i].y = 0;
        circles[i].prevX = 0;
        circles[i].prevY = 0;
        circles[i].smoothedX = 0.0f;
        circles[i].smoothedY = 0.0f;
        circles[i].velX = 0.0f;
        circles[i].velY = 0.0f;
        circles[i].z = 0.0f;
        circles[i].scaledRadius = 0;
    }
}

void Simulator::clearCircleArea(int prevX, int prevY, int radius) {
    for (int i = prevX - radius; i <= prevX + radius; i++) {
        for (int j = prevY - radius; j <= prevY + radius; j++) {
            if (i > 0 && i < gridX-1 && j > 0 && j < gridY-1) {
                float dx = (i + 0.5f) - prevX;
                float dy = (j + 0.5f) - prevY;
                float dist = sqrt(dx * dx + dy * dy);
                if (dist <= radius) {
                    s[idx(i, j)] = 1.0f; // make fluid
                    d[idx(i, j)] = 1.0f;
                    x[idx(i, j)] = 0.0f;
                    y[idx(i, j)] = 0.0f;
                }
            }
        }
    }
}

void Simulator::updateLineSegments(const FingertipData* landmarks, int count) {
    // requires updateCircles to have run first (segment endpoints use smoothed circle positions)
    int numHands = std::min(2, count / HandTracking::LANDMARKS_PER_HAND);

    numSegments = 0;
    numPresentSegments = 0;
    for (int hand = 0; hand < numHands; hand++) {
        int offset = hand * HandTracking::LANDMARKS_PER_HAND;

        bool handPresent = false;
        for (int i = 0; i < HandTracking::LANDMARKS_PER_HAND; i++) {
            if (landmarks[offset + i].present > 0.5f) {
                handPresent = true;
                break;
            }
        }
        if (!handPresent) continue;

        // create segments for each connection
        for (int conn = 0; conn < HandTracking::MAX_CONNECTIONS && numSegments < HandTracking::MAX_SEGMENTS; conn++) {
            int idx1 = HandTracking::HAND_CONNECTIONS[conn][0];
            int idx2 = HandTracking::HAND_CONNECTIONS[conn][1];

            const FingertipData& p1 = landmarks[offset + idx1];
            const FingertipData& p2 = landmarks[offset + idx2];

            LineSegment& seg = segments[numSegments];
            seg.wasPresent = seg.present;
            seg.prevStartX = seg.startX;
            seg.prevStartY = seg.startY;
            seg.prevEndX = seg.endX;
            seg.prevEndY = seg.endY;
            seg.prevStartRadius = seg.startRadius;
            seg.prevEndRadius = seg.endRadius;

            seg.present = (p1.present > 0.5f) && (p2.present > 0.5f);

            if (seg.present) {
                int startIdx = offset + idx1;
                int endIdx = offset + idx2;
                seg.startX = circles[startIdx].x;
                seg.startY = circles[startIdx].y;
                seg.endX = circles[endIdx].x;
                seg.endY = circles[endIdx].y;
                seg.startRadius = scaleRadiusByZ(p1.z);
                seg.endRadius = scaleRadiusByZ(p2.z);
                numPresentSegments++;
            } else if (seg.wasPresent) {
                seg.startX = 0;
                seg.startY = 0;
                seg.endX = 0;
                seg.endY = 0;
            }

            numSegments++;
        }
    }

    for (int i = numSegments; i < HandTracking::MAX_SEGMENTS; i++) {
        segments[i].present = false;
        segments[i].wasPresent = false;
        segments[i].startX = 0;
        segments[i].startY = 0;
        segments[i].endX = 0;
        segments[i].endY = 0;
        segments[i].startRadius = 0.0f;
        segments[i].endRadius = 0.0f;
    }
}

bool Simulator::isPointNearSegment(int px, int py, int x1, int y1, float r1, int x2, int y2, float r2) {
    // vector from p1 to p2
    float dx = static_cast<float>(x2 - x1);
    float dy = static_cast<float>(y2 - y1);
    float length = sqrt(dx * dx + dy * dy);

    if (length < 0.001f) {
        // point
        float dist = sqrt(static_cast<float>((px - x1) * (px - x1) + (py - y1) * (py - y1)));
        return dist <= r1;
    }

    // normalized direction
    float nx = dx / length;
    float ny = dy / length;

    // vector from p1 to test point
    float tx = static_cast<float>(px - x1);
    float ty = static_cast<float>(py - y1);

    // project onto line
    float t = tx * nx + ty * ny;
    t = std::max(0.0f, std::min(length, t));

    // closest point on segment
    float cx = x1 + t * nx;
    float cy = y1 + t * ny;

    // interpolate radius at closest point
    float radius = r1 + (r2 - r1) * (t / length);

    // distance from closest point
    float dist = sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));

    return dist <= radius;
}

void Simulator::applyLineSegmentSolidFieldFullGrid() {
    for (int i = 1; i < gridX - 1; i++) {
        for (int j = 1; j < gridY - 1; j++) {
            bool isInAnySegment = false;
            bool wasInAnyPrevSegment = false;

            for (int sIdx = 0; sIdx < numSegments; sIdx++) {
                const LineSegment& seg = segments[sIdx];
                if (!seg.present) continue;

                bool wasInPrev = isPointNearSegment(
                    i, j,
                    seg.prevStartX, seg.prevStartY, seg.prevStartRadius,
                    seg.prevEndX, seg.prevEndY, seg.prevEndRadius);
                bool isInNew = isPointNearSegment(
                    i, j,
                    seg.startX, seg.startY, seg.startRadius,
                    seg.endX, seg.endY, seg.endRadius);

                if (wasInPrev && !isInNew) {
                    d[idx(i, j)] = 1.0f;
                }
                if (wasInPrev) {
                    wasInAnyPrevSegment = true;
                }
                if (isInNew) {
                    isInAnySegment = true;
                }
            }

            if (wasInAnyPrevSegment) {
                x[idx(i, j)] = 0.0f;
                y[idx(i, j)] = 0.0f;
            }
            if (isInAnySegment) {
                x[idx(i, j)] = 0.0f;
                y[idx(i, j)] = 0.0f;
                s[idx(i, j)] = 0.0f;
            } else {
                s[idx(i, j)] = 1.0f;
            }
        }
    }
}

void Simulator::applyCircleSolids() {
    // Line-segment mode owns the full interior solid field; circles only update local footprints.
    if (numPresentSegments == 0) {
        for (int i = 1; i < gridX - 1; i++) {
            for (int j = 1; j < gridY - 1; j++) {
                s[idx(i, j)] = 1.0f;
            }
        }
    }

    for (int c = 0; c < numCircles; c++) {
        const CircleState& circle = circles[c];
        if (circle.present) {
            updateCircleAreas(circle.prevX, circle.prevY, circle.x, circle.y,
                               circle.scaledRadius, circle.scaledRadius);
        } else if (circle.wasPresent) {
            clearCircleArea(circle.prevX, circle.prevY, circle.scaledRadius);
        }
    }

    for (int c = numCircles; c < HandTracking::MAX_CIRCLES; c++) {
        const CircleState& circle = circles[c];
        if (circle.wasPresent) {
            clearCircleArea(circle.prevX, circle.prevY, circle.scaledRadius);
        }
    }
}

void Simulator::clearMousePullFootprint() {
    clearCircleArea(mouseCircleX, mouseCircleY, mouseCircleRadius);
    clearCircleArea(mousePrevCircleX, mousePrevCircleY, mouseCircleRadius);
}

void Simulator::applyInput() {
    if (!isMouseInput(g_config.inputMode) && numPresentSegments > 0) {
        applyLineSegmentSolidFieldFullGrid();
    }

    if (isMouseInput(g_config.inputMode)) {
        // Pull mode: momentum only, never carve solids (matches compute_circle.wgsl)
        if (isMouseDragging) {
            clearMousePullFootprint();
        }
    } else if (numCircles > 0) {
        applyCircleSolids();
    }

    circleMomentumTransfer();
}

void Simulator::update() {
    applyInput();
    enforceBoundaryConditions();
    project();
    extrapolate();
    advect();
    if (doVorticity) {
        applyVorticity();
    }
    enforceBoundaryConditions();

    if (isMouseInput(g_config.inputMode)) {
        if (isMouseDragging) {
            mousePrevCircleX = mouseCircleX;
            mousePrevCircleY = mouseCircleY;
        }
    } else if (numCircles > 0) {
        for (int i = 0; i < numCircles; i++) {
            circles[i].prevX = circles[i].x;
            circles[i].prevY = circles[i].y;
            if (!circles[i].present) {
                circles[i].wasPresent = false;
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
        newInk = ink;
    }
    int cy = gridY / 2;

    #pragma omp parallel for
    for (int i = 1; i < gridX - 1; i++) {
        for (int j = 1; j < gridY - 1; j++) {
            if (s[idx(i, j)] != 0.0f) {
                // x vel advection
                if (i > 0 && j < gridY - 1 && s[idx(i - 1, j)] != 0.0f) {
                    bool advectX = true;
                    if (i + 1 < gridX) {
                        advectX = s[idx(i + 1, j)] != 0.0f;
                    }
                    if (advectX) {
                        float x0 = i * cellSize;
                        float y0 = j * cellSize + halfCellSize;
                        x0 -= x[idx(i, j)] * timestep;
                        y0 -= neighborhoodY(i, j) * timestep;
                        newX[idx(i, j)] = sample(x0, y0, 0);
                    }
                }

                // y vel advection
                if (j > 0 && i < gridX - 1 && s[idx(i, j - 1)] != 0.0f) {
                    bool advectY = true;
                    if (j + 1 < gridY) {
                        advectY = s[idx(i, j + 1)] != 0.0f;
                    }
                    if (advectY) {
                        float x0 = i * cellSize + halfCellSize;
                        float y0 = j * cellSize;
                        x0 -= neighborhoodX(i, j) * timestep;
                        y0 -= y[idx(i, j)] * timestep;
                        newY[idx(i, j)] = sample(x0, y0, 1);
                    }
                }

                // smoke advection
                float x0 = (x[idx(i, j)] + x[idx(i + 1, j)]) / 2.0f;
                float y0 = (y[idx(i, j)] + y[idx(i, j + 1)]) / 2.0f;
                float x1 = i * cellSize + halfCellSize - x0 * timestep;
                float y1 = j * cellSize + halfCellSize - y0 * timestep;
                newD[idx(i, j)] = sample(x1, y1, 2);

                // ink advection
                if (inkInitialized) {
                    float vel_x = (x[idx(i, j)] + x[idx(i + 1, j)]) / 2.0f;
                    float vel_y = (y[idx(i, j)] + y[idx(i, j + 1)]) / 2.0f;

                    float inkX0 = i * cellSize + halfCellSize - vel_x * timestep;
                    float inkY0 = j * cellSize + halfCellSize - vel_y * timestep;

                    float ir, ig, ib, ia;
                    sampleInk(inkX0, inkY0, ink, ir, ig, ib, ia);
                    int base = inkCellBase(i, j);
                    newInk[base] = ir;
                    newInk[base + 1] = ig;
                    newInk[base + 2] = ib;
                    newInk[base + 3] = ia;
                }
            }
        }
    }

    x = newX;
    y = newY;
    d = newD;
    if (inkInitialized) {
        ink = newInk;
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

float Simulator::clamp(float n, float min, float max) const {
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

float Simulator::hash2D(int px, int py) {
    float x = static_cast<float>(px) * 0.1031f;
    float y = static_cast<float>(py) * 0.1030f;
    float h = std::fmod(x * 127.1f + y * 311.7f, 1.0f);
    if (h < 0.0f) h += 1.0f;
    float result = std::fmod(h * 43758.5453f, 1.0f);
    if (result < 0.0f) result += 1.0f;
    return result;
}

bool Simulator::isNearWindTunnelBoundary(int i, int j) const {
    if (windTunnelSide == -1) return false;

    if (windTunnelSide == 0) {
        return i <= 2 && j >= windTunnelStartCell && j < windTunnelEndCell;
    } else if (windTunnelSide == 1) {
        return j >= gridY - 3 && i >= windTunnelStartCell && i < windTunnelEndCell;
    } else if (windTunnelSide == 2) {
        return j <= 2 && i >= windTunnelStartCell && i < windTunnelEndCell;
    } else if (windTunnelSide == 3) {
        return i >= gridX - 3 && j >= windTunnelStartCell && j < windTunnelEndCell;
    }
    return false;
}

void Simulator::getWindTunnelInk(int i, int j, const std::vector<float>& field,
                                 float& r, float& g, float& b, float& a) const {
    int velSeedX = static_cast<int>(x[idx(i, j)] * 1000.0f);
    int velSeedY = static_cast<int>(y[idx(i, j)] * 1000.0f);

    float randX = hash2D(i + velSeedX, j + velSeedY);
    float randY = hash2D(j + velSeedY, i + velSeedX);
    int sampleI = static_cast<int>(randX * static_cast<float>(gridX));
    int sampleJ = static_cast<int>(randY * static_cast<float>(gridY));

    sampleI = std::max(0, std::min(gridX - 1, sampleI));
    sampleJ = std::max(0, std::min(gridY - 1, sampleJ));

    int base = inkCellBase(sampleI, sampleJ);
    r = field[base];
    g = field[base + 1];
    b = field[base + 2];
    a = field[base + 3];
}

void Simulator::sampleInk(float xPos, float yPos, const std::vector<float>& field,
                          float& r, float& g, float& b, float& a) const {
    float xClamp = clamp(xPos, halfCellSize, static_cast<float>(gridX) * cellSize - halfCellSize);
    float yClamp = clamp(yPos, halfCellSize, static_cast<float>(gridY) * cellSize - halfCellSize);

    float gx = (xClamp - halfCellSize) / cellSize;
    float gy = (yClamp - halfCellSize) / cellSize;

    int i0 = static_cast<int>(std::floor(gx));
    int j0 = static_cast<int>(std::floor(gy));
    int i1 = std::min(i0 + 1, gridX - 1);
    int j1 = std::min(j0 + 1, gridY - 1);

    float fx = gx - static_cast<float>(i0);
    float fy = gy - static_cast<float>(j0);

    auto loadInk = [&](int ci, int cj, float& cr, float& cg, float& cb, float& ca) {
        if (isNearWindTunnelBoundary(ci, cj)) {
            getWindTunnelInk(ci, cj, field, cr, cg, cb, ca);
        } else {
            int base = inkCellBase(ci, cj);
            cr = field[base];
            cg = field[base + 1];
            cb = field[base + 2];
            ca = field[base + 3];
        }
    };

    float r00, g00, b00, a00;
    float r10, g10, b10, a10;
    float r01, g01, b01, a01;
    float r11, g11, b11, a11;
    loadInk(i0, j0, r00, g00, b00, a00);
    loadInk(i1, j0, r10, g10, b10, a10);
    loadInk(i0, j1, r01, g01, b01, a01);
    loadInk(i1, j1, r11, g11, b11, a11);

    auto mix1 = [](float a0, float a1, float t) { return a0 * (1.0f - t) + a1 * t; };
    r = mix1(mix1(r00, r10, fx), mix1(r01, r11, fx), fy);
    g = mix1(mix1(g00, g10, fx), mix1(g01, g11, fx), fy);
    b = mix1(mix1(b00, b10, fx), mix1(b01, b11, fx), fy);
    a = mix1(mix1(a00, a10, fx), mix1(a01, a11, fx), fy);
}


// CIRCLE HELPERS
void Simulator::moveCircle(int newGridX, int newGridY) {
    float instantVelX = (newGridX - mouseCircleX) / timestep;
    float instantVelY = (newGridY - mouseCircleY) / timestep;
    mouseCircleVelX = instantVelX;
    mouseCircleVelY = instantVelY;

    mouseCircleX = newGridX;
    mouseCircleY = newGridY;
}

void Simulator::enforceBoundaryConditions() {
    int edgesMask = config->simulation.edges;
    bool leftEdge = edgesMask & 8;
    bool topEdge = edgesMask & 4;
    bool bottomEdge = edgesMask & 2;
    bool rightEdge = edgesMask & 1;

    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            if (i == 0) {
                if (leftEdge) x[idx(i, j)] = 0.0f;
                else if (gridX > 1) x[idx(i, j)] = x[idx(i + 1, j)];
            }
            if (i == gridX - 1) {
                if (rightEdge) x[idx(i, j)] = 0.0f;
                else if (gridX > 1) x[idx(i, j)] = x[idx(i - 1, j)];
            }
            if (j == 0) {
                if (bottomEdge) y[idx(i, j)] = 0.0f;
                else if (gridY > 1) y[idx(i, j)] = y[idx(i, j + 1)];
            }
            if (j == gridY - 1) {
                if (topEdge) y[idx(i, j)] = 0.0f;
                else if (gridY > 1) y[idx(i, j)] = y[idx(i, j - 1)];
            }
        }
    }

    // Zero velocity samples on MAC faces shared with solid neighbors.
    for (int i = 0; i < gridX; i++) {
        for (int j = 0; j < gridY; j++) {
            if (s[idx(i, j)] == 0.0f) {
                x[idx(i, j)] = 0.0f;
                y[idx(i, j)] = 0.0f;
            } else {
                if (i > 0 && s[idx(i - 1, j)] == 0.0f) {
                    x[idx(i, j)] = 0.0f;
                }
                if (j > 0 && s[idx(i, j - 1)] == 0.0f) {
                    y[idx(i, j)] = 0.0f;
                }
            }
        }
    }

    // preserve wind tunnel velocity
    // Force only the wall<->fluid interface face per side (MAC grid: x/y live on
    // the left/bottom face). On left/bottom the outer wall face (index 0) is also
    // pinned harmlessly; on right/top forcing the inner face (gridX-2 / gridY-2)
    // would cancel the inlet cell's divergence and kill the jet, so it is omitted.
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
                        x[idx(gridX-1, j)] = -windTunnelSpeed;
                        d[idx(gridX-1, j)] = 0.0f;
                    }
                }
                break;
        }
    }
}

void Simulator::circleMomentumTransfer() {
    if (isMouseInput(g_config.inputMode)) {
        if (!isMouseDragging) return;

        // Pull mode: single circle while dragging
        int deltaX = mouseCircleX - mousePrevCircleX;
        int deltaY = mouseCircleY - mousePrevCircleY;
        if (deltaX == 0 && deltaY == 0) return;

        float effectiveRadius = mouseCircleRadius + momentumTransferRadius;

        // apply momentum to fluid cells near ball surface
        for (int i = mouseCircleX - static_cast<int>(effectiveRadius) - 1;
             i <= mouseCircleX + static_cast<int>(effectiveRadius) + 1; i++) {
            for (int j = mouseCircleY - static_cast<int>(effectiveRadius) - 1;
                 j <= mouseCircleY + static_cast<int>(effectiveRadius) + 1; j++) {

                if (i >= 0 && i < gridX && j >= 0 && j < gridY) {
                    float dx = (i + 0.5f) - mouseCircleX;
                    float dy = (j + 0.5f) - mouseCircleY;
                    float distance = sqrt(dx * dx + dy * dy);

                    float prevDx = (i + 0.5f) - mousePrevCircleX;
                    float prevDy = (j + 0.5f) - mousePrevCircleY;
                    float prevDistance = sqrt(prevDx * prevDx + prevDy * prevDy);
                    bool wasInPrev = prevDistance <= static_cast<float>(mouseCircleRadius);
                    bool isInCircle = distance <= static_cast<float>(mouseCircleRadius);

                    // within influence radius, outside current and previous cursor disks
                    if (!isInCircle && !wasInPrev && distance <= effectiveRadius) {
                        // falloff is 1/r^2
                        float normalizedDistance = (distance - mouseCircleRadius) / momentumTransferRadius;
                        float falloff = 1.0f - normalizedDistance * normalizedDistance;
                        falloff = std::max(0.0f, falloff);

                        float densityFactor = d[idx(i, j)]; // weight velocity imparted by local density

                        float momentumX = deltaX * momentumTransferStrength * falloff * densityFactor;
                        float momentumY = deltaY * momentumTransferStrength * falloff * densityFactor;

                        x[idx(i, j)] += momentumX;
                        y[idx(i, j)] += momentumY;

                        // NOTE [DISABLED]: max velocity clamping for force imparted by circle, for stability
                        // float maxVel = 8.0f;
                        // x[idx(i, j)] = std::max(-maxVel, std::min(maxVel, x[idx(i, j)]));
                        // y[idx(i, j)] = std::max(-maxVel, std::min(maxVel, y[idx(i, j)]));
                    }
                }
            }
        }
    } else {
        // Hand mode: multiple circles
        const HandSensitivityParams handSensitivity = resolveHandSensitivity(config->simulation.circle);
        for (int c = 0; c < HandTracking::MAX_CIRCLES; c++) {
            const CircleState& circle = circles[c];
            if (!circle.present) continue;
            if (!shouldApplyMomentumTransferVelocity(circle.velX, circle.velY, handSensitivity)) continue;

            const float impulseScale = computeMomentumImpulseScale(
                circle.velX, circle.velY, handSensitivity);
            if (impulseScale <= 0.0f) continue;

            float effectiveRadius = circle.scaledRadius + momentumTransferRadius;

            // apply momentum to fluid cells near ball surface
            for (int i = circle.x - static_cast<int>(effectiveRadius) - 1;
                 i <= circle.x + static_cast<int>(effectiveRadius) + 1; i++) {
                for (int j = circle.y - static_cast<int>(effectiveRadius) - 1;
                     j <= circle.y + static_cast<int>(effectiveRadius) + 1; j++) {

                    if (i >= 0 && i < gridX && j >= 0 && j < gridY) {
                        if (s[idx(i, j)] == 0.0f) continue;

                        float dx = (i + 0.5f) - circle.x;
                        float dy = (j + 0.5f) - circle.y;
                        float distance = sqrt(dx * dx + dy * dy);

                        float prevDx = (i + 0.5f) - circle.prevX;
                        float prevDy = (j + 0.5f) - circle.prevY;
                        float prevDistance = sqrt(prevDx * prevDx + prevDy * prevDy);
                        bool wasInPrevCircle = prevDistance <= circle.scaledRadius;

                        // within influence radius but outside ball and not exiting solid footprint
                        if (distance > circle.scaledRadius && distance <= effectiveRadius && !wasInPrevCircle) {
                            // falloff is 1/r^2
                            float normalizedDistance = (distance - circle.scaledRadius) / momentumTransferRadius;
                            float falloff = 1.0f - normalizedDistance * normalizedDistance;
                            falloff = std::max(0.0f, falloff);

                            float densityFactor = d[idx(i, j)]; // weight velocity imparted by local density

                            float momentumX = circle.velX * timestep * momentumTransferStrength * falloff * densityFactor * impulseScale;
                            float momentumY = circle.velY * timestep * momentumTransferStrength * falloff * densityFactor * impulseScale;

                            x[idx(i, j)] += momentumX;
                            y[idx(i, j)] += momentumY;

                            // NOTE [DISABLED]: max velocity clamping for force imparted by circle, for stability
                            // float maxVel = 8.0f;
                            // x[idx(i, j)] = std::max(-maxVel, std::min(maxVel, x[idx(i, j)]));
                            // y[idx(i, j)] = std::max(-maxVel, std::min(maxVel, y[idx(i, j)]));
                        }
                    }
                }
            }
        }
    }
}

void Simulator::updateCircleAreas(int prevX, int prevY, int newX, int newY,
                                    int prevRadius, int newRadius) {
    // bounding box surrounding new and old circles
    int minI = std::min(prevX - prevRadius, newX - newRadius);
    int maxI = std::max(prevX + prevRadius, newX + newRadius);
    int minJ = std::min(prevY - prevRadius, newY - newRadius);
    int maxJ = std::max(prevY + prevRadius, newY + newRadius);

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

                bool wasInPrevCircle = distPrev <= prevRadius;
                bool isInNewCircle = distNew <= newRadius;

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

void Simulator::applyScalarParams(const Config& config) {
    timestep = config.simulation.timestep;
    overrelaxationCoefficient = config.simulation.projection.overrelaxationCoefficient;
    projectionIters = config.simulation.projection.iterations;
    doVorticity = config.simulation.vorticity.strength > 0.0f;
    vorticity = config.simulation.vorticity.strength;
    vorticityLen = config.simulation.vorticity.lengthScale;
    windTunnelSide = config.simulation.windTunnel.side;
    windTunnelSpeed = config.simulation.windTunnel.velocity;
    recomputeWindTunnelCells(config);
    momentumTransferStrength = config.simulation.circle.momentumTransferStrength;
    momentumTransferRadius = config.simulation.circle.momentumTransferRadius;

    mouseCircleRadius = static_cast<int>(config.simulation.circle.radius / cellSize);
    baseCircleRadius = mouseCircleRadius;

    this->config = &g_config;
}

void Simulator::updateSimParams(const Config& config) {
    if (gridConfigChanged(config)) {
        rebuildGridFromConfig(config);
    }
    applyScalarParams(config);
}

void Simulator::reinitInk(const ImageData* imageData) {
    // Reset fluid state to zero velocity, density, pressure
    resetFluidState();

    if (imageData) {
        initializeFromImageData(*this->config, imageData);
    }
    inkInitialized = (imageData != nullptr);
}

void Simulator::resetFluidState(bool clearInk) {
    // Zero velocity/density/pressure arrays
    std::fill(x.begin(), x.end(), 0.0f);
    std::fill(y.begin(), y.end(), 0.0f);
    std::fill(d.begin(), d.end(), 1.0f);
    std::fill(p.begin(), p.end(), 0.0f);
    // Note: s (solid) is NOT zeroed - boundaries must be preserved

    // Zero ink arrays if they exist and clearInk is true
    if (clearInk && !ink.empty()) {
        std::fill(ink.begin(), ink.end(), 0.0f);
    }

    // Reset circle state
    // Reset mouse circle
    mouseCircleX = gridX / 2;
    mouseCircleY = gridY / 2;
    mousePrevCircleX = mouseCircleX;
    mousePrevCircleY = mouseCircleY;
    mouseCircleVelX = 0.0f;
    mouseCircleVelY = 0.0f;
    isMouseDragging = false;

    // Reset hand circles and segments
    numCircles = 0;
    for (int i = 0; i < HandTracking::MAX_CIRCLES; i++) {
        circles[i].present = false;
        circles[i].wasPresent = false;
        circles[i].x = 0;
        circles[i].y = 0;
        circles[i].prevX = 0;
        circles[i].prevY = 0;
        circles[i].velX = 0.0f;
        circles[i].velY = 0.0f;
        circles[i].z = 0.0f;
        circles[i].scaledRadius = baseCircleRadius;
    }
    for (int i = 0; i < HandTracking::MAX_SEGMENTS; i++) {
        segments[i].present = false;
        segments[i].wasPresent = false;
        segments[i].startX = 0;
        segments[i].startY = 0;
        segments[i].endX = 0;
        segments[i].endY = 0;
        segments[i].prevStartX = 0;
        segments[i].prevStartY = 0;
        segments[i].prevEndX = 0;
        segments[i].prevEndY = 0;
        segments[i].startRadius = 0.0f;
        segments[i].endRadius = 0.0f;
        segments[i].prevStartRadius = 0.0f;
        segments[i].prevEndRadius = 0.0f;
    }
    numSegments = 0;

    // Reapply edges (boundary conditions)
    int edgesMask = config->simulation.edges;
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
            } else if (s[idx(i, j)] == 0.0f) {
                // Restore solid cells that aren't on edges
                s[idx(i, j)] = 1.0f;
            }
        }
    }

    // Reapply wind tunnel
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
                    y[idx(i, gridY-1)] = -windTunnelSpeed;
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
}
