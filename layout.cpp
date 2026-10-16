#include "layout.h"
#include "sim_shared.h"
#include <algorithm>
#include <array>
#include <vector>

namespace { // private

enum class AspectAnchor {
    Center,
    BottomRight,
    BottomCenter,
    Left,
    Right,
};

struct Quadrants {
    PixelRect topLeft;
    PixelRect topRight;
    PixelRect bottomLeft;
    PixelRect bottomRight;
};

struct RowOrigin {
    int startX;
    int rowY;
};

struct PlotCameraGridLayout {
    std::vector<PixelRect> plotSlots;
    PixelRect cameraArea = makeRect(0, 0, 0, 0);
    bool valid = true;
};

// layout constants
constexpr const char CAMERA_FRAME[] = "camera_frame";
const std::array<const char*, 4> VIEWPORT_NAMES = {
    "viewport_1", "viewport_2", "viewport_3", "viewport_4"
};
const std::array<const char*, 4> PLOT_PANEL_ORDER = {
    "density_histogram",
    "velocity_histogram",
    "entropy_time_series",
    "volume_time_series"
};
const std::array<const char*, 4> PLOT_GRID_ORDER = {
    "density_histogram",
    "entropy_time_series",
    "velocity_histogram",
    "volume_time_series"
};

// rect geometry
PixelRect insetRect(const PixelRect& rect, int padX, int padY) {
    PixelRect inset = rect;
    inset.x += padX;
    inset.y += padY;
    inset.width = std::max(0, rect.width - 2 * padX);
    inset.height = std::max(0, rect.height - 2 * padY);
    return inset;
}

PixelRect fitRectToAspect(const PixelRect& slot, float targetAspectRatio, AspectAnchor anchor) {
    if (slot.width <= 0 || slot.height <= 0 || targetAspectRatio <= 0.0f) {
        return slot;
    }

    PixelRect fitted = slot;
    const float slotAspectRatio = static_cast<float>(slot.width) / static_cast<float>(slot.height);

    if (slotAspectRatio > targetAspectRatio) {
        fitted.width = std::max(0, static_cast<int>(slot.height * targetAspectRatio));
        fitted.height = slot.height;
        switch (anchor) {
            case AspectAnchor::Center:
            case AspectAnchor::BottomCenter:
                fitted.x = slot.x + (slot.width - fitted.width) / 2;
                break;
            case AspectAnchor::BottomRight:
            case AspectAnchor::Right:
                fitted.x = slot.x + (slot.width - fitted.width);
                break;
            case AspectAnchor::Left:
                fitted.x = slot.x;
                break;
        }
        fitted.y = slot.y;
    } else {
        fitted.width = slot.width;
        fitted.height = std::max(0, static_cast<int>(slot.width / targetAspectRatio));
        fitted.x = slot.x;
        switch (anchor) {
            case AspectAnchor::Center:
            case AspectAnchor::Left:
            case AspectAnchor::Right:
                fitted.y = slot.y + (slot.height - fitted.height) / 2;
                break;
            case AspectAnchor::BottomRight:
            case AspectAnchor::BottomCenter:
                fitted.y = slot.y + (slot.height - fitted.height);
                break;
        }
        if (anchor == AspectAnchor::BottomCenter) {
            fitted.x = slot.x + (slot.width - fitted.width) / 2;
        }
    }

    return fitted;
}

Quadrants splitRect2x2(const PixelRect& area) {
    const int cellWidth = std::max(0, area.width / 2);
    const int cellHeight = std::max(0, area.height / 2);
    Quadrants quads = {};
    quads.topLeft = makeRect(area.x, area.y, cellWidth, cellHeight);
    quads.topRight = makeRect(area.x + cellWidth, area.y, area.width - cellWidth, cellHeight);
    quads.bottomLeft = makeRect(area.x, area.y + cellHeight, cellWidth, area.height - cellHeight);
    quads.bottomRight = makeRect(area.x + cellWidth, area.y + cellHeight, area.width - cellWidth, area.height - cellHeight);
    return quads;
}

std::vector<PixelRect> splitRectRows(const PixelRect& area, int rowCount) {
    std::vector<PixelRect> rows;
    if (rowCount <= 0 || area.width <= 0 || area.height <= 0) {
        return rows;
    }

    const int eachH = std::max(0, area.height / rowCount);
    rows.reserve(static_cast<size_t>(rowCount));
    for (int row = 0; row < rowCount; ++row) {
        const int y = area.y + (row * eachH);
        const int height = (row == rowCount - 1) ? (area.height - (rowCount - 1) * eachH) : eachH;
        rows.push_back(makeRect(area.x, y, area.width, height));
    }
    return rows;
}

int computeRowHeight(const PixelRect& bundleArea, float widthPerRowHeight) {
    if (widthPerRowHeight <= 0.0f) {
        return 0;
    }
    return std::max(1, std::min(
        bundleArea.height,
        static_cast<int>(bundleArea.width / widthPerRowHeight)
    ));
}

RowOrigin computeCenteredRowOrigin(const PixelRect& bundleArea, int rowHeight, int totalWidth) {
    return {
        bundleArea.x + std::max(0, (bundleArea.width - totalWidth) / 2),
        bundleArea.y + std::max(0, (bundleArea.height - rowHeight) / 2),
    };
}

PixelRect buildViewportFrame(int canvasW, int canvasH, float viewportAspectRatio) {
    PixelRect fullFrame = makeRect(0, 0, canvasW, canvasH);
    if (viewportAspectRatio <= 0.0f) {
        return fullFrame;
    }
    return fitRectToAspect(fullFrame, viewportAspectRatio, AspectAnchor::Center);
}

Quadrants buildFrameAndQuadrants(int canvasW, int canvasH, float viewportAspectRatio) {
    const PixelRect frame = buildViewportFrame(canvasW, canvasH, viewportAspectRatio);
    return splitRect2x2(frame);
}

std::vector<PixelRect> computeGalleryTopSlots(const PixelRect& topArea,
                                              float viewportAspectRatio,
                                              int topViewportCount) {
    if (topViewportCount == 1) {
        PixelRect slot = topArea;
        if (viewportAspectRatio > 0.0f && topArea.width > 0 && topArea.height > 0) {
            slot = fitRectToAspect(topArea, viewportAspectRatio, AspectAnchor::BottomCenter);
        }
        return {slot};
    }

    if (topViewportCount == 2) {
        PixelRect left = makeRect(topArea.x, topArea.y, std::max(0, topArea.width / 2), topArea.height);
        PixelRect right = makeRect(topArea.x + left.width, topArea.y, topArea.width - left.width, topArea.height);
        if (viewportAspectRatio > 0.0f && topArea.width > 0 && topArea.height > 0) {
            const int eachHeight = std::min(
                topArea.height,
                static_cast<int>(topArea.width / (2.0f * viewportAspectRatio))
            );
            const int eachWidth = static_cast<int>(eachHeight * viewportAspectRatio);
            const int groupWidth = eachWidth * 2;
            const int startX = topArea.x + (topArea.width - groupWidth) / 2;
            const int y = topArea.y + (topArea.height - eachHeight);
            left = makeRect(startX, y, eachWidth, eachHeight);
            right = makeRect(startX + eachWidth, y, eachWidth, eachHeight);
        }
        return {left, right};
    }

    Quadrants quads = splitRect2x2(topArea);
    if (viewportAspectRatio > 0.0f && topArea.width > 0 && topArea.height > 0) {
        const int eachHeight = std::min(
            topArea.height / 2,
            static_cast<int>(topArea.width / (2.0f * viewportAspectRatio))
        );
        const int eachWidth = static_cast<int>(eachHeight * viewportAspectRatio);
        const int groupWidth = eachWidth * 2;
        const int groupHeight = eachHeight * 2;
        const int startX = topArea.x + (topArea.width - groupWidth) / 2;
        const int startY = topArea.y + (topArea.height - groupHeight);

        quads.topLeft = makeRect(startX, startY, eachWidth, eachHeight);
        quads.topRight = makeRect(startX + eachWidth, startY, eachWidth, eachHeight);
        quads.bottomLeft = makeRect(startX, startY + eachHeight, eachWidth, eachHeight);
        quads.bottomRight = makeRect(startX + eachWidth, startY + eachHeight, eachWidth, eachHeight);
    }
    return {quads.topLeft, quads.topRight, quads.bottomLeft, quads.bottomRight};
}

PixelRect cameraAreaBesideTopPlots(const PixelRect& bundleArea, int halfW, int halfH) {
    return makeRect(
        bundleArea.x + halfW,
        bundleArea.y + halfH,
        std::max(0, bundleArea.width - halfW),
        std::max(0, bundleArea.height - halfH)
    );
}

PlotCameraGridLayout computePlotCameraGridLayout(const PixelRect& bundleArea, int count) {
    PlotCameraGridLayout layout = {};
    layout.cameraArea = bundleArea;
    const int halfW = std::max(0, bundleArea.width / 2);
    const int halfH = std::max(0, bundleArea.height / 2);

    if (count >= 4) {
        const int plotWidth = halfW;
        const int rowHeight = std::max(0, bundleArea.height / 3);
        if (plotWidth <= 0 || rowHeight <= 0) {
            layout.valid = false;
            return layout;
        }
        layout.plotSlots = {
            makeRect(bundleArea.x, bundleArea.y, plotWidth, rowHeight),
            makeRect(bundleArea.x + plotWidth, bundleArea.y, plotWidth, rowHeight),
            makeRect(bundleArea.x, bundleArea.y + rowHeight, plotWidth, rowHeight),
            makeRect(bundleArea.x, bundleArea.y + rowHeight * 2, plotWidth, bundleArea.height - 2 * rowHeight)
        };
        layout.cameraArea = makeRect(
            bundleArea.x + plotWidth,
            bundleArea.y + rowHeight,
            std::max(0, bundleArea.width - plotWidth),
            std::max(0, bundleArea.height - rowHeight)
        );
        return layout;
    }

    if (count == 3) {
        const Quadrants plotQuads = splitRect2x2(bundleArea);
        layout.plotSlots = {plotQuads.topLeft, plotQuads.topRight, plotQuads.bottomLeft};
        layout.cameraArea = cameraAreaBesideTopPlots(bundleArea, halfW, halfH);
        return layout;
    }

    if (count == 2) {
        layout.plotSlots = {
            makeRect(bundleArea.x, bundleArea.y, halfW, halfH),
            makeRect(bundleArea.x + halfW, bundleArea.y, bundleArea.width - halfW, halfH)
        };
        layout.cameraArea = cameraAreaBesideTopPlots(bundleArea, halfW, halfH);
        return layout;
    }

    if (count == 1) {
        const int topH = halfH;
        layout.plotSlots = {makeRect(bundleArea.x, bundleArea.y, bundleArea.width, topH)};
        layout.cameraArea = makeRect(
            bundleArea.x,
            bundleArea.y + topH,
            bundleArea.width,
            std::max(0, bundleArea.height - topH)
        );
    }

    return layout;
}


// component visibility
void hideComponentIfPresent(const LayoutConfig& config, const std::string& name);

std::vector<std::string> getEnabledPlotPanels(const LayoutConfig& config,
                                              const std::array<const char*, 4>& order) {
    std::vector<std::string> enabled;
    for (const auto& name : order) {
        auto it = config.components.find(name);
        if (it != config.components.end() && it->second.histogramEnabled) {
            enabled.push_back(name);
        }
    }
    return enabled;
}

void hideAllPlotPanels(const LayoutConfig& config) {
    for (const auto& name : PLOT_PANEL_ORDER) {
        hideComponentIfPresent(config, name);
    }
}

void hideNonActivePlotPanels(const LayoutConfig& config, const std::vector<std::string>& activePlots) {
    for (const auto& name : PLOT_PANEL_ORDER) {
        if (std::find(activePlots.begin(), activePlots.end(), name) == activePlots.end()) {
            hideComponentIfPresent(config, name);
        }
    }
}

void hideComponentIfPresent(const LayoutConfig& config, const std::string& name) {
    if (config.components.find(name) == config.components.end()) {
        return;
    }
    g_layoutPixels.components[name] = makeRect(0, 0, 0, 0);
}

void hideAllPlotAndCameraPanels(const LayoutConfig& config) {
    hideAllPlotPanels(config);
    hideComponentIfPresent(config, CAMERA_FRAME);
}

void hideAllViewports(const LayoutConfig& config) {
    for (const auto& name : VIEWPORT_NAMES) {
        hideComponentIfPresent(config, name);
    }
}


// rect placement
void applyRectWithPadding(const LayoutConfig& config, const std::string& name, const PixelRect& rect) {
    auto it = config.components.find(name);
    if (it == config.components.end()) {
        return;
    }
    g_layoutPixels.components[name] = insetRect(rect, it->second.px, it->second.py);
}

void applyPlotsToSlots(const LayoutConfig& config,
                       const std::vector<std::string>& plotNames,
                       const std::vector<PixelRect>& slots) {
    const int count = std::min(static_cast<int>(plotNames.size()), static_cast<int>(slots.size()));
    for (int i = 0; i < count; ++i) {
        applyRectWithPadding(config, plotNames[i], slots[i]);
    }
}

void placeCameraFrame(const LayoutConfig& config,
                      const PixelRect& area,
                      float cameraAspectRatio,
                      AspectAnchor anchor) {
    if (area.width <= 0 || area.height <= 0) {
        hideComponentIfPresent(config, CAMERA_FRAME);
        return;
    }
    const float aspect = safeCameraAspectRatio(cameraAspectRatio);
    applyRectWithPadding(config, CAMERA_FRAME, fitRectToAspect(area, aspect, anchor));
}

int normalizeRotation(int rotation) {
    rotation %= 4;
    if (rotation < 0) {
        rotation += 4;
    }
    return rotation;
}

float effectiveViewportAspect(float viewportAspectRatio,
                              const PixelRect& slot,
                              int rotation) {
    float aspect = viewportAspectRatio;
    if (aspect <= 0.0f && slot.width > 0 && slot.height > 0) {
        aspect = static_cast<float>(slot.width) / static_cast<float>(slot.height);
    }
    if (aspect > 0.0f && rotation % 2 == 1) {
        aspect = 1.0f / aspect;
    }
    return aspect;
}

AspectAnchor viewportAnchorForOddRotation(const PixelRect& slot, int canvasW) {
    const float slotCenterX = slot.x + slot.width / 2.0f;
    const bool onRightSide = (2 * slotCenterX) > canvasW;
    return onRightSide ? AspectAnchor::Left : AspectAnchor::Right;
}

void applyViewportRect(const LayoutConfig& config,
                       const std::string& name,
                       const PixelRect& slot,
                       float viewportAspectRatio,
                       int canvasW) {
    int rotation = 0;
    auto it = config.components.find(name);
    if (it != config.components.end()) {
        rotation = normalizeRotation(it->second.rotation);
    }

    PixelRect fitted = slot;
    const float aspect = effectiveViewportAspect(viewportAspectRatio, slot, rotation);
    if (aspect > 0.0f) {
        if (rotation % 2 == 1) {
            fitted = fitRectToAspect(slot, aspect, viewportAnchorForOddRotation(slot, canvasW));
        } else {
            fitted = fitRectToAspect(slot, aspect, AspectAnchor::Center);
        }
    }
    applyRectWithPadding(config, name, fitted);
}


// preset placement
void placePlotGridBundle2x2(const LayoutConfig& config, const PixelRect& bundleArea) {
    const std::vector<std::string> plotsGridOrder = getEnabledPlotPanels(config, PLOT_GRID_ORDER);
    const std::vector<std::string> plotsColumnOrder = getEnabledPlotPanels(config, PLOT_PANEL_ORDER);
    hideNonActivePlotPanels(config, plotsGridOrder);

    const int count = static_cast<int>(plotsGridOrder.size());
    if (count <= 0 || bundleArea.width <= 0 || bundleArea.height <= 0) {
        hideAllPlotPanels(config);
        return;
    }

    std::vector<PixelRect> slots;
    if (count >= 4) {
        const Quadrants grid = splitRect2x2(bundleArea);
        slots = {grid.topLeft, grid.topRight, grid.bottomLeft, grid.bottomRight};
    } else if (count == 3) {
        slots = splitRectRows(bundleArea, 3);
    } else if (count == 2) {
        slots = splitRectRows(bundleArea, 2);
    } else {
        slots = {bundleArea};
    }

    const std::vector<std::string>& orderedPlots = (count == 3) ? plotsColumnOrder : plotsGridOrder;
    applyPlotsToSlots(config, orderedPlots, slots);
}

void placePlotCameraGridBundle(const LayoutConfig& config,
                               const PixelRect& bundleArea,
                               float cameraAspectRatio) {
    const std::vector<std::string> plots = getEnabledPlotPanels(config, PLOT_GRID_ORDER);
    hideNonActivePlotPanels(config, plots);

    if (bundleArea.width <= 0 || bundleArea.height <= 0) {
        hideAllPlotAndCameraPanels(config);
        return;
    }

    const int count = static_cast<int>(plots.size());
    PlotCameraGridLayout layout = computePlotCameraGridLayout(bundleArea, count);
    if (!layout.valid) {
        hideAllPlotAndCameraPanels(config);
        return;
    }

    applyPlotsToSlots(config, plots, layout.plotSlots);

    if (count <= 0) {
        hideAllPlotPanels(config);
        layout.cameraArea = bundleArea;
    }

    placeCameraFrame(config, layout.cameraArea, cameraAspectRatio, AspectAnchor::BottomRight);
}

void placePlotRow(const LayoutConfig& config,
                  const PixelRect& bundleArea,
                  const std::vector<std::string>& plots,
                  float cameraAspectRatio,
                  bool includeCamera) {
    const float aspect = safeCameraAspectRatio(cameraAspectRatio);
    const int count = static_cast<int>(plots.size());

    if (count <= 0) {
        hideAllPlotPanels(config);
        if (includeCamera) {
            placeCameraFrame(config, bundleArea, aspect, AspectAnchor::Center);
        }
        return;
    }

    const float widthPerRowHeight = includeCamera
        ? aspect + (DEFAULT_PLOT_ASPECT_RATIO * static_cast<float>(count))
        : DEFAULT_PLOT_ASPECT_RATIO * static_cast<float>(count);
    if (widthPerRowHeight <= 0.0f) {
        if (includeCamera) {
            hideAllPlotAndCameraPanels(config);
        } else {
            hideAllPlotPanels(config);
        }
        return;
    }

    const int rowHeight = computeRowHeight(bundleArea, widthPerRowHeight);
    const int plotWidth = std::max(1, static_cast<int>(DEFAULT_PLOT_ASPECT_RATIO * rowHeight));
    const int cameraWidth = includeCamera ? std::max(1, static_cast<int>(aspect * rowHeight)) : 0;
    const int totalWidth = cameraWidth + (count * plotWidth);
    if (totalWidth <= 0) {
        if (includeCamera) {
            hideAllPlotAndCameraPanels(config);
        } else {
            hideAllPlotPanels(config);
        }
        return;
    }

    const RowOrigin origin = computeCenteredRowOrigin(bundleArea, rowHeight, totalWidth);
    if (includeCamera) {
        PixelRect cameraSlot = makeRect(origin.startX, origin.rowY, cameraWidth, rowHeight);
        applyRectWithPadding(
            config,
            CAMERA_FRAME,
            fitRectToAspect(cameraSlot, aspect, AspectAnchor::Center)
        );
    }

    for (int i = 0; i < count; ++i) {
        const int plotX = origin.startX + cameraWidth + (i * plotWidth);
        PixelRect slot = makeRect(plotX, origin.rowY, plotWidth, rowHeight);
        applyRectWithPadding(config, plots[i], slot);
    }
}

void placePlotCameraRowBundle(const LayoutConfig& config,
                              const PixelRect& bundleArea,
                              float cameraAspectRatio) {
    if (bundleArea.width <= 0 || bundleArea.height <= 0) {
        hideAllPlotAndCameraPanels(config);
        return;
    }

    const std::vector<std::string> plots = getEnabledPlotPanels(config, PLOT_PANEL_ORDER);
    hideNonActivePlotPanels(config, plots);

    if (!config.camerasEnabled) {
        hideComponentIfPresent(config, CAMERA_FRAME);
        placePlotRow(config, bundleArea, plots, cameraAspectRatio, false);
        return;
    }

    placePlotRow(config, bundleArea, plots, cameraAspectRatio, true);
}

void applyDefaultPreset(const LayoutConfig& config,
                        int canvasW,
                        int canvasH,
                        float viewportAspectRatio,
                        float cameraAspectRatio) {
    const Quadrants layout = buildFrameAndQuadrants(canvasW, canvasH, viewportAspectRatio);

    if (config.camerasEnabled) {
        placePlotCameraGridBundle(config, layout.topLeft, cameraAspectRatio);
    } else {
        hideComponentIfPresent(config, CAMERA_FRAME);
        placePlotGridBundle2x2(config, layout.topLeft);
    }
    applyViewportRect(config, VIEWPORT_NAMES[0], layout.topRight, viewportAspectRatio, canvasW);
    applyViewportRect(config, VIEWPORT_NAMES[1], layout.bottomLeft, viewportAspectRatio, canvasW);
    applyViewportRect(config, VIEWPORT_NAMES[2], layout.bottomRight, viewportAspectRatio, canvasW);
    hideComponentIfPresent(config, VIEWPORT_NAMES[3]);
}

void applyFocusedPreset(const LayoutConfig& config,
                        int canvasW,
                        int canvasH,
                        float viewportAspectRatio,
                        float cameraAspectRatio) {
    const Quadrants layout = buildFrameAndQuadrants(canvasW, canvasH, viewportAspectRatio);
    hideAllPlotAndCameraPanels(config);

    if (config.camerasEnabled) {
        placeCameraFrame(config, layout.topLeft, cameraAspectRatio, AspectAnchor::Center);
    } else {
        hideComponentIfPresent(config, CAMERA_FRAME);
    }
    placePlotGridBundle2x2(config, layout.bottomLeft);
    applyViewportRect(config, VIEWPORT_NAMES[0], layout.topRight, viewportAspectRatio, canvasW);
    applyViewportRect(config, VIEWPORT_NAMES[1], layout.bottomRight, viewportAspectRatio, canvasW);
    hideComponentIfPresent(config, VIEWPORT_NAMES[2]);
    hideComponentIfPresent(config, VIEWPORT_NAMES[3]);
}

void applyViewportPreset(const LayoutConfig& config,
                         int canvasW,
                         int canvasH,
                         float viewportAspectRatio) {
    hideAllPlotAndCameraPanels(config);
    const Quadrants layout = buildFrameAndQuadrants(canvasW, canvasH, viewportAspectRatio);
    applyViewportRect(config, VIEWPORT_NAMES[0], layout.topLeft, viewportAspectRatio, canvasW);
    applyViewportRect(config, VIEWPORT_NAMES[1], layout.topRight, viewportAspectRatio, canvasW);
    applyViewportRect(config, VIEWPORT_NAMES[2], layout.bottomLeft, viewportAspectRatio, canvasW);
    applyViewportRect(config, VIEWPORT_NAMES[3], layout.bottomRight, viewportAspectRatio, canvasW);
}

void applyGalleryPreset(const LayoutConfig& config,
                        int canvasW,
                        int canvasH,
                        float viewportAspectRatio,
                        float cameraAspectRatio,
                        int topViewportCount) {
    hideAllPlotAndCameraPanels(config);
    hideAllViewports(config);

    const int bottomHeight = std::max(1, static_cast<int>(canvasH * 0.15f));
    const int topHeight = std::max(0, canvasH - bottomHeight);

    const PixelRect topArea = makeRect(0, 0, canvasW, topHeight);
    const PixelRect bottomArea = makeRect(0, topHeight, canvasW, bottomHeight);

    placePlotCameraRowBundle(config, bottomArea, cameraAspectRatio);
    const std::vector<PixelRect> topSlots = computeGalleryTopSlots(topArea, viewportAspectRatio, topViewportCount);
    for (int i = 0; i < static_cast<int>(topSlots.size()) && i < static_cast<int>(VIEWPORT_NAMES.size()); ++i) {
        applyViewportRect(config, VIEWPORT_NAMES[i], topSlots[i], 0.0f, canvasW);
    }
}


// preset dispatch
using PresetApplyFn = void(*)(const LayoutConfig&, int, int, float, float);

void dispatchViewportPreset(const LayoutConfig& config,
                            int canvasW,
                            int canvasH,
                            float viewportAspectRatio,
                            float cameraAspectRatio) {
    (void)cameraAspectRatio;
    applyViewportPreset(config, canvasW, canvasH, viewportAspectRatio);
}

void dispatchGallerySingle(const LayoutConfig& config,
                           int canvasW,
                           int canvasH,
                           float viewportAspectRatio,
                           float cameraAspectRatio) {
    applyGalleryPreset(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio, 1);
}

void dispatchGalleryDouble(const LayoutConfig& config,
                           int canvasW,
                           int canvasH,
                           float viewportAspectRatio,
                           float cameraAspectRatio) {
    applyGalleryPreset(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio, 2);
}

void dispatchGalleryQuad(const LayoutConfig& config,
                         int canvasW,
                         int canvasH,
                         float viewportAspectRatio,
                         float cameraAspectRatio) {
    applyGalleryPreset(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio, 4);
}

struct PresetDispatchEntry {
    const char* name;
    PresetApplyFn apply;
    bool inkOnly = false;
};

const PresetDispatchEntry PRESET_DISPATCH[] = {
    {LAYOUT_PRESET_DEFAULT, applyDefaultPreset},
    {LAYOUT_PRESET_FOCUSED, applyFocusedPreset},
    {LAYOUT_PRESET_VIEWPORT, dispatchViewportPreset},
    {LAYOUT_PRESET_GALLERY_SINGLE, dispatchGallerySingle},
    {LAYOUT_PRESET_GALLERY_DOUBLE, dispatchGalleryDouble},
    {LAYOUT_PRESET_GALLERY_QUAD, dispatchGalleryQuad},
    {LAYOUT_PRESET_LEGACY, applyDefaultPreset, true},
};

}  // namespace

void applyDefaultComponentLayout(const LayoutConfig& config, int canvasW, int canvasH) {
    for (const auto& [name, bbox] : config.components) {
        PixelRect rect;
        rect.x = static_cast<int>(bbox.x * canvasW) + bbox.px;
        rect.y = static_cast<int>(bbox.y * canvasH) + bbox.py;
        rect.width = std::max(0, static_cast<int>(bbox.w * canvasW) - 2 * bbox.px);
        rect.height = std::max(0, static_cast<int>(bbox.h * canvasH) - 2 * bbox.py);
        g_layoutPixels.components[name] = rect;
    }
}

void applyLayoutPreset(const LayoutConfig& config,
                       int canvasW,
                       int canvasH,
                       float viewportAspectRatio,
                       float cameraAspectRatio,
                       bool isInkMode) {
    const std::string preset = config.preset.empty() ? LAYOUT_PRESET_DEFAULT : config.preset;
    for (const auto& entry : PRESET_DISPATCH) {
        if (preset != entry.name) {
            continue;
        }
        if (entry.inkOnly && !isInkMode) {
            break;
        }
        entry.apply(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio);
        return;
    }
    applyDefaultPreset(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio);
}

std::string layoutPixelsToJson(const LayoutPixels& pixels, const LayoutConfig& config) {
    json j;
    for (const auto& [name, rect] : pixels.components) {
        j[name] = {
            {"x", rect.x},
            {"y", rect.y},
            {"width", rect.width},
            {"height", rect.height}
        };
    }
    for (const auto& [name, bbox] : config.components) {
        if (j.contains(name)) {
            j[name]["rotation"] = bbox.rotation;
            j[name]["viewportTarget"] = bbox.viewportTarget;
            j[name]["viewportVelocityViewEnabled"] = bbox.viewportVelocityViewEnabled;
            j[name]["histogramEnabled"] = bbox.histogramEnabled;
        }
    }
    return j.dump();
}
