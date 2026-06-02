#include "config.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>

using json = nlohmann::json;

namespace {
PixelRect insetRect(const PixelRect& rect, int padX, int padY) {
    PixelRect inset = rect;
    inset.x += padX;
    inset.y += padY;
    inset.width = std::max(0, rect.width - 2 * padX);
    inset.height = std::max(0, rect.height - 2 * padY);
    return inset;
}

PixelRect fitRectToAspectCentered(const PixelRect& slot, float targetAspectRatio) {
    if (slot.width <= 0 || slot.height <= 0 || targetAspectRatio <= 0.0f) {
        return slot;
    }

    PixelRect fitted = slot;
    const float slotAspectRatio = static_cast<float>(slot.width) / static_cast<float>(slot.height);

    if (slotAspectRatio > targetAspectRatio) {
        fitted.width = std::max(0, static_cast<int>(slot.height * targetAspectRatio));
        fitted.height = slot.height;
        fitted.x = slot.x + (slot.width - fitted.width) / 2;
        fitted.y = slot.y;
    } else {
        fitted.width = slot.width;
        fitted.height = std::max(0, static_cast<int>(slot.width / targetAspectRatio));
        fitted.x = slot.x;
        fitted.y = slot.y + (slot.height - fitted.height) / 2;
    }

    return fitted;
}

PixelRect fitRectToAspectBottomRight(const PixelRect& slot, float targetAspectRatio) {
    if (slot.width <= 0 || slot.height <= 0 || targetAspectRatio <= 0.0f) {
        return slot;
    }

    PixelRect fitted = slot;
    const float slotAspectRatio = static_cast<float>(slot.width) / static_cast<float>(slot.height);

    if (slotAspectRatio > targetAspectRatio) {
        fitted.width = std::max(0, static_cast<int>(slot.height * targetAspectRatio));
        fitted.height = slot.height;
        fitted.x = slot.x + (slot.width - fitted.width);
        fitted.y = slot.y;
    } else {
        fitted.width = slot.width;
        fitted.height = std::max(0, static_cast<int>(slot.width / targetAspectRatio));
        fitted.x = slot.x;
        fitted.y = slot.y + (slot.height - fitted.height);
    }

    return fitted;
}

constexpr float kPlotAspectRatio = 1.5f;
const std::vector<std::string> kViewportNames = {
    "viewport_1", "viewport_2", "viewport_3", "viewport_4"
};
const std::vector<std::string> kPlotPanelOrder = {
    "density_histogram",
    "velocity_histogram",
    "entropy_time_series",
    "volume_time_series"
};
const std::vector<std::string> kPlotGridOrder = {
    "density_histogram",
    "entropy_time_series",
    "velocity_histogram",
    "volume_time_series"
};
void hideComponentIfPresent(const LayoutConfig& config, const std::string& name);

std::vector<std::string> getEnabledPlotPanels(const LayoutConfig& config) {
    std::vector<std::string> enabled;
    for (const auto& name : kPlotPanelOrder) {
        auto it = config.components.find(name);
        if (it != config.components.end() && it->second.enabled) {
            enabled.push_back(name);
        }
    }
    return enabled;
}

std::vector<std::string> getEnabledPlotPanelsGridOrder(const LayoutConfig& config) {
    std::vector<std::string> enabled;
    for (const auto& name : kPlotGridOrder) {
        auto it = config.components.find(name);
        if (it != config.components.end() && it->second.enabled) {
            enabled.push_back(name);
        }
    }
    return enabled;
}

void hideAllPlotPanels(const LayoutConfig& config) {
    for (const auto& name : kPlotPanelOrder) {
        hideComponentIfPresent(config, name);
    }
}

void hideNonActivePlotPanels(const LayoutConfig& config, const std::vector<std::string>& activePlots) {
    for (const auto& name : kPlotPanelOrder) {
        if (std::find(activePlots.begin(), activePlots.end(), name) == activePlots.end()) {
            hideComponentIfPresent(config, name);
        }
    }
}

void hideComponentIfPresent(const LayoutConfig& config, const std::string& name) {
    if (config.components.find(name) == config.components.end()) {
        return;
    }
    g_layoutPixels.components[name] = {0, 0, 0, 0};
}

void hideAllPlotAndCameraPanels(const LayoutConfig& config) {
    hideAllPlotPanels(config);
    hideComponentIfPresent(config, "camera_frame");
}

void hideAllViewports(const LayoutConfig& config) {
    for (const auto& name : kViewportNames) {
        hideComponentIfPresent(config, name);
    }
}

void applyRectWithPadding(const LayoutConfig& config, const std::string& name, const PixelRect& rect) {
    auto it = config.components.find(name);
    if (it == config.components.end()) {
        return;
    }
    g_layoutPixels.components[name] = insetRect(rect, it->second.px, it->second.py);
}

void applyViewportRect(const LayoutConfig& config,
                       const std::string& name,
                       const PixelRect& slot,
                       float viewportAspectRatio) {
    PixelRect fitted = slot;
    if (viewportAspectRatio > 0.0f) {
        fitted = fitRectToAspectCentered(slot, viewportAspectRatio);
    }
    applyRectWithPadding(config, name, fitted);
}

PixelRect buildViewportFrame(int canvasW, int canvasH, float viewportAspectRatio) {
    PixelRect fullFrame = {0, 0, canvasW, canvasH};
    if (viewportAspectRatio <= 0.0f) {
        return fullFrame;
    }
    return fitRectToAspectCentered(fullFrame, viewportAspectRatio);
}

struct Quadrants {
    PixelRect topLeft;
    PixelRect topRight;
    PixelRect bottomLeft;
    PixelRect bottomRight;
};

Quadrants splitIntoQuadrants(const PixelRect& frame) {
    const int cellWidth = std::max(0, frame.width / 2);
    const int cellHeight = std::max(0, frame.height / 2);
    Quadrants quads = {};
    quads.topLeft = {frame.x, frame.y, cellWidth, cellHeight};
    quads.topRight = {frame.x + cellWidth, frame.y, frame.width - cellWidth, cellHeight};
    quads.bottomLeft = {frame.x, frame.y + cellHeight, cellWidth, frame.height - cellHeight};
    quads.bottomRight = {frame.x + cellWidth, frame.y + cellHeight, frame.width - cellWidth, frame.height - cellHeight};
    return quads;
}

void placePlotGridBundle2x2(const LayoutConfig& config, const PixelRect& bundleArea) {
    const std::vector<std::string> plotsGridOrder = getEnabledPlotPanelsGridOrder(config);
    const std::vector<std::string> plotsColumnOrder = getEnabledPlotPanels(config);
    hideNonActivePlotPanels(config, plotsGridOrder);

    const int count = static_cast<int>(plotsGridOrder.size());
    if (count <= 0 || bundleArea.width <= 0 || bundleArea.height <= 0) {
        hideAllPlotPanels(config);
        return;
    }

    std::vector<PixelRect> slots;
    if (count >= 4) {
        const int halfW = std::max(0, bundleArea.width / 2);
        const int halfH = std::max(0, bundleArea.height / 2);
        slots = {
            {bundleArea.x, bundleArea.y, halfW, halfH},
            {bundleArea.x + halfW, bundleArea.y, bundleArea.width - halfW, halfH},
            {bundleArea.x, bundleArea.y + halfH, halfW, bundleArea.height - halfH},
            {bundleArea.x + halfW, bundleArea.y + halfH, bundleArea.width - halfW, bundleArea.height - halfH}
        };
    } else if (count == 3) {
        const int eachH = std::max(0, bundleArea.height / 3);
        slots = {
            {bundleArea.x, bundleArea.y, bundleArea.width, eachH},
            {bundleArea.x, bundleArea.y + eachH, bundleArea.width, eachH},
            {bundleArea.x, bundleArea.y + 2 * eachH, bundleArea.width, bundleArea.height - 2 * eachH}
        };
    } else if (count == 2) {
        const int halfH = std::max(0, bundleArea.height / 2);
        slots = {
            {bundleArea.x, bundleArea.y, bundleArea.width, halfH},
            {bundleArea.x, bundleArea.y + halfH, bundleArea.width, bundleArea.height - halfH}
        };
    } else {
        slots = {bundleArea};
    }

    const std::vector<std::string>& orderedPlots = (count == 3) ? plotsColumnOrder : plotsGridOrder;
    for (int i = 0; i < count && i < static_cast<int>(slots.size()); ++i) {
        applyRectWithPadding(config, orderedPlots[i], slots[i]);
    }
}

void placePlotCameraGridBundle(const LayoutConfig& config,
                               const PixelRect& bundleArea,
                               float cameraAspectRatio) {
    const std::vector<std::string> plots = getEnabledPlotPanelsGridOrder(config);
    hideNonActivePlotPanels(config, plots);

    if (bundleArea.width <= 0 || bundleArea.height <= 0) {
        hideAllPlotAndCameraPanels(config);
        return;
    }

    const int count = static_cast<int>(plots.size());
    const int halfW = std::max(0, bundleArea.width / 2);
    const int halfH = std::max(0, bundleArea.height / 2);
    const float safeCameraAspectRatio = cameraAspectRatio > 0.0f ? cameraAspectRatio : (4.0f / 3.0f);

    std::vector<PixelRect> plotSlots;
    PixelRect cameraArea = bundleArea;

    if (count >= 4) {
        const int plotWidth = halfW;
        const int rowHeight = std::max(0, bundleArea.height / 3);
        if (plotWidth <= 0 || rowHeight <= 0) {
            hideAllPlotAndCameraPanels(config);
            return;
        }
        plotSlots = {
            {bundleArea.x, bundleArea.y, plotWidth, rowHeight},
            {bundleArea.x + plotWidth, bundleArea.y, plotWidth, rowHeight},
            {bundleArea.x, bundleArea.y + rowHeight, plotWidth, rowHeight},
            {bundleArea.x, bundleArea.y + rowHeight * 2, plotWidth, bundleArea.height - 2 * rowHeight}
        };
        cameraArea = {
            bundleArea.x + plotWidth,
            bundleArea.y + rowHeight,
            std::max(0, bundleArea.width - plotWidth),
            std::max(0, bundleArea.height - rowHeight)
        };
    } else if (count == 3) {
        plotSlots = {
            {bundleArea.x, bundleArea.y, halfW, halfH},
            {bundleArea.x + halfW, bundleArea.y, bundleArea.width - halfW, halfH},
            {bundleArea.x, bundleArea.y + halfH, halfW, bundleArea.height - halfH}
        };
        cameraArea = {
            bundleArea.x + halfW,
            bundleArea.y + halfH,
            std::max(0, bundleArea.width - halfW),
            std::max(0, bundleArea.height - halfH)
        };
    } else if (count == 2) {
        plotSlots = {
            {bundleArea.x, bundleArea.y, halfW, halfH},
            {bundleArea.x + halfW, bundleArea.y, bundleArea.width - halfW, halfH}
        };
        cameraArea = {
            bundleArea.x + halfW,
            bundleArea.y + halfH,
            std::max(0, bundleArea.width - halfW),
            std::max(0, bundleArea.height - halfH)
        };
    } else if (count == 1) {
        const int topH = halfH;
        plotSlots = {
            {bundleArea.x, bundleArea.y, bundleArea.width, topH}
        };
        cameraArea = {
            bundleArea.x,
            bundleArea.y + topH,
            bundleArea.width,
            std::max(0, bundleArea.height - topH)
        };
    }

    for (int i = 0; i < count && i < static_cast<int>(plotSlots.size()); ++i) {
        applyRectWithPadding(config, plots[i], plotSlots[i]);
    }

    if (count <= 0) {
        hideAllPlotPanels(config);
        cameraArea = bundleArea;
    }

    if (cameraArea.width <= 0 || cameraArea.height <= 0) {
        hideComponentIfPresent(config, "camera_frame");
        return;
    }
    applyRectWithPadding(config, "camera_frame", fitRectToAspectBottomRight(cameraArea, safeCameraAspectRatio));
}

void placePlotCameraRowBundle(const LayoutConfig& config,
                              const PixelRect& bundleArea,
                              float cameraAspectRatio) {
    if (bundleArea.width <= 0 || bundleArea.height <= 0) {
        hideAllPlotAndCameraPanels(config);
        return;
    }

    const std::vector<std::string> plots = getEnabledPlotPanels(config);
    hideNonActivePlotPanels(config, plots);

    const float safeCameraAspectRatio = cameraAspectRatio > 0.0f ? cameraAspectRatio : (4.0f / 3.0f);
    const int count = static_cast<int>(plots.size());

    if (!config.camerasEnabled) {
        hideComponentIfPresent(config, "camera_frame");
        if (count <= 0) {
            hideAllPlotPanels(config);
            return;
        }

        const float widthPerRowHeight = kPlotAspectRatio * static_cast<float>(count);
        if (widthPerRowHeight <= 0.0f) {
            hideAllPlotPanels(config);
            return;
        }

        const int rowHeight = std::max(1, std::min(
            bundleArea.height,
            static_cast<int>(bundleArea.width / widthPerRowHeight)
        ));
        const int plotWidth = std::max(1, static_cast<int>(kPlotAspectRatio * rowHeight));
        const int totalWidth = count * plotWidth;
        if (totalWidth <= 0) {
            hideAllPlotPanels(config);
            return;
        }

        const int startX = bundleArea.x + std::max(0, (bundleArea.width - totalWidth) / 2);
        const int rowY = bundleArea.y + std::max(0, (bundleArea.height - rowHeight) / 2);

        for (int i = 0; i < count; ++i) {
            PixelRect slot = {startX + (i * plotWidth), rowY, plotWidth, rowHeight};
            applyRectWithPadding(config, plots[i], slot);
        }
        return;
    }

    if (count <= 0) {
        hideAllPlotPanels(config);
        applyRectWithPadding(config, "camera_frame", fitRectToAspectCentered(bundleArea, safeCameraAspectRatio));
        return;
    }

    const float widthPerRowHeight = safeCameraAspectRatio + (kPlotAspectRatio * static_cast<float>(count));
    if (widthPerRowHeight <= 0.0f) {
        hideAllPlotAndCameraPanels(config);
        return;
    }

    const int rowHeight = std::max(1, std::min(
        bundleArea.height,
        static_cast<int>(bundleArea.width / widthPerRowHeight)
    ));
    const int cameraWidth = std::max(1, static_cast<int>(safeCameraAspectRatio * rowHeight));
    const int plotWidth = std::max(1, static_cast<int>(kPlotAspectRatio * rowHeight));
    const int totalWidth = cameraWidth + (count * plotWidth);
    if (totalWidth <= 0) {
        hideAllPlotAndCameraPanels(config);
        return;
    }

    const int startX = bundleArea.x + std::max(0, (bundleArea.width - totalWidth) / 2);
    const int rowY = bundleArea.y + std::max(0, (bundleArea.height - rowHeight) / 2);

    PixelRect cameraSlot = {startX, rowY, cameraWidth, rowHeight};
    applyRectWithPadding(config, "camera_frame", fitRectToAspectCentered(cameraSlot, safeCameraAspectRatio));

    for (int i = 0; i < count; ++i) {
        PixelRect slot = {startX + cameraWidth + (i * plotWidth), rowY, plotWidth, rowHeight};
        applyRectWithPadding(config, plots[i], slot);
    }
}

void applyDefaultPreset(const LayoutConfig& config,
                        int canvasW,
                        int canvasH,
                        float viewportAspectRatio,
                        float cameraAspectRatio) {
    const PixelRect frame = buildViewportFrame(canvasW, canvasH, viewportAspectRatio);
    const Quadrants quads = splitIntoQuadrants(frame);

    if (config.camerasEnabled) {
        placePlotCameraGridBundle(config, quads.topLeft, cameraAspectRatio);
    } else {
        hideComponentIfPresent(config, "camera_frame");
        placePlotGridBundle2x2(config, quads.topLeft);
    }
    applyViewportRect(config, "viewport_1", quads.topRight, viewportAspectRatio);
    applyViewportRect(config, "viewport_2", quads.bottomLeft, viewportAspectRatio);
    applyViewportRect(config, "viewport_3", quads.bottomRight, viewportAspectRatio);
    hideComponentIfPresent(config, "viewport_4");
}

void applyFocusedPreset(const LayoutConfig& config,
                        int canvasW,
                        int canvasH,
                        float viewportAspectRatio,
                        float cameraAspectRatio) {
    const PixelRect frame = buildViewportFrame(canvasW, canvasH, viewportAspectRatio);
    const Quadrants quads = splitIntoQuadrants(frame);
    hideAllPlotAndCameraPanels(config);

    if (config.camerasEnabled) {
        const float safeCameraAspectRatio = cameraAspectRatio > 0.0f ? cameraAspectRatio : (4.0f / 3.0f);
        applyRectWithPadding(config, "camera_frame", fitRectToAspectCentered(quads.topLeft, safeCameraAspectRatio));
    } else {
        hideComponentIfPresent(config, "camera_frame");
    }
    placePlotGridBundle2x2(config, quads.bottomLeft);
    applyViewportRect(config, "viewport_1", quads.topRight, viewportAspectRatio);
    applyViewportRect(config, "viewport_2", quads.bottomRight, viewportAspectRatio);
    hideComponentIfPresent(config, "viewport_3");
    hideComponentIfPresent(config, "viewport_4");
}

void applyViewportPreset(const LayoutConfig& config,
                         int canvasW,
                         int canvasH,
                         float viewportAspectRatio) {
    hideAllPlotAndCameraPanels(config);
    const PixelRect frame = buildViewportFrame(canvasW, canvasH, viewportAspectRatio);
    const Quadrants quads = splitIntoQuadrants(frame);
    applyViewportRect(config, "viewport_1", quads.topLeft, viewportAspectRatio);
    applyViewportRect(config, "viewport_2", quads.topRight, viewportAspectRatio);
    applyViewportRect(config, "viewport_3", quads.bottomLeft, viewportAspectRatio);
    applyViewportRect(config, "viewport_4", quads.bottomRight, viewportAspectRatio);
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

    PixelRect topArea = {0, 0, canvasW, topHeight};
    PixelRect bottomArea = {0, topHeight, canvasW, bottomHeight};

    placePlotCameraRowBundle(config, bottomArea, cameraAspectRatio);

    if (topViewportCount == 1) {
        PixelRect slot = topArea;
        if (viewportAspectRatio > 0.0f && topArea.width > 0 && topArea.height > 0) {
            int height = std::min(topArea.height, static_cast<int>(topArea.width / viewportAspectRatio));
            int width = static_cast<int>(height * viewportAspectRatio);
            slot = {
                topArea.x + (topArea.width - width) / 2,
                topArea.y + (topArea.height - height),
                width,
                height
            };
        }
        applyViewportRect(config, "viewport_1", slot, 0.0f);
        return;
    }

    if (topViewportCount == 2) {
        PixelRect left = {topArea.x, topArea.y, std::max(0, topArea.width / 2), topArea.height};
        PixelRect right = {topArea.x + left.width, topArea.y, topArea.width - left.width, topArea.height};
        if (viewportAspectRatio > 0.0f && topArea.width > 0 && topArea.height > 0) {
            int eachHeight = std::min(topArea.height, static_cast<int>(topArea.width / (2.0f * viewportAspectRatio)));
            int eachWidth = static_cast<int>(eachHeight * viewportAspectRatio);
            int groupWidth = eachWidth * 2;
            int startX = topArea.x + (topArea.width - groupWidth) / 2;
            int y = topArea.y + (topArea.height - eachHeight);
            left = {startX, y, eachWidth, eachHeight};
            right = {startX + eachWidth, y, eachWidth, eachHeight};
        }
        applyViewportRect(config, "viewport_1", left, 0.0f);
        applyViewportRect(config, "viewport_2", right, 0.0f);
        return;
    }

    PixelRect topLeft = {topArea.x, topArea.y, std::max(0, topArea.width / 2), std::max(0, topArea.height / 2)};
    PixelRect topRight = {topArea.x + topLeft.width, topArea.y, topArea.width - topLeft.width, topLeft.height};
    PixelRect bottomLeft = {topArea.x, topArea.y + topLeft.height, topLeft.width, topArea.height - topLeft.height};
    PixelRect bottomRight = {topArea.x + topLeft.width, topArea.y + topLeft.height, topArea.width - topLeft.width, topArea.height - topLeft.height};

    if (viewportAspectRatio > 0.0f && topArea.width > 0 && topArea.height > 0) {
        int eachHeight = std::min(topArea.height / 2, static_cast<int>(topArea.width / (2.0f * viewportAspectRatio)));
        int eachWidth = static_cast<int>(eachHeight * viewportAspectRatio);
        int groupWidth = eachWidth * 2;
        int groupHeight = eachHeight * 2;
        int startX = topArea.x + (topArea.width - groupWidth) / 2;
        int startY = topArea.y + (topArea.height - groupHeight);

        topLeft = {startX, startY, eachWidth, eachHeight};
        topRight = {startX + eachWidth, startY, eachWidth, eachHeight};
        bottomLeft = {startX, startY + eachHeight, eachWidth, eachHeight};
        bottomRight = {startX + eachWidth, startY + eachHeight, eachWidth, eachHeight};
    }

    applyViewportRect(config, "viewport_1", topLeft, 0.0f);
    applyViewportRect(config, "viewport_2", topRight, 0.0f);
    applyViewportRect(config, "viewport_3", bottomLeft, 0.0f);
    applyViewportRect(config, "viewport_4", bottomRight, 0.0f);
}
}  // namespace

Config ConfigLoader::loadConfig(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "ERROR: Cannot open config file: " << filename << std::endl;
            exit(1);
        }

        json j;
        file >> j;
        file.close();

        Config config;
        config.pipeline = stringToPipelineType(j["pipeline"]);
        config.window = loadWindowConfig(j["window"]);
        config.simulation = loadSimulationConfig(j["simulation"]);
        config.rendering = loadRenderingConfig(j["rendering"]);
        config.ink = loadInkConfig(j["ink"]);
        config.layout = loadLayoutConfig(j["layout"]);

        return config;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "CONFIG ERROR: " << e.what() << std::endl;
        std::cerr << "Ensure config.json has all required keys." << std::endl;
        exit(1);
    }
}

PipelineType ConfigLoader::stringToPipelineType(const std::string& type) {
    if (type == "host") {
        return PipelineType::CPU;
    } else if (type == "device") {
        return PipelineType::GPU;
    } else if (type == "hybrid") {
        return PipelineType::HYBRID;
    }
    return PipelineType::CPU;
}

WindowConfig ConfigLoader::loadWindowConfig(const json& j) {
    WindowConfig config;
    config.baseSize = j["baseSize"];
    config.defaultWidth = j["defaultWidth"];
    config.defaultHeight = j["defaultHeight"];
    return config;
}

SimulationConfig ConfigLoader::loadSimulationConfig(const json& j) {
    SimulationConfig config;
    config.resolution = j["resolution"];
    config.timestep = j["timestep"];
    config.gravity = j["gravity"];
    config.fluidDensity = j["fluidDensity"];
    config.edges = j["edges"];

    config.projection = loadProjectionConfig(j["projection"]);
    config.vorticity = loadVorticityConfig(j["vorticity"]);
    config.windTunnel = loadWindTunnelConfig(j["windTunnel"]);
    config.circle = loadCircleConfig(j["circle"]);

    return config;
}

RenderingConfig ConfigLoader::loadRenderingConfig(const json& j) {
    RenderingConfig config;
    config.target = j["target"];
    config.showVelocityVectors = j["showVelocityVectors"];
    config.disableHistograms = j["disableHistograms"];
    config.velocityScale = j["velocityScale"];
    return config;
}

InkConfig ConfigLoader::loadInkConfig(const json& j) {
    InkConfig config;
    config.imagePath = j["imagePath"];
    return config;
}

LayoutConfig ConfigLoader::loadLayoutConfig(const json& j) {
    LayoutConfig config;
    config.preset = j["preset"];
    config.camerasEnabled = j.value("camerasEnabled", true);

    for (auto& [key, val] : j["components"].items()) {
        ComponentBBox bbox;
        bbox.x = val["x"];
        bbox.y = val["y"];
        bbox.w = val["w"];
        bbox.h = val["h"];
        bbox.px = val["px"];
        bbox.py = val["py"];
        bbox.target = val.value("target", 2);
        bbox.enabled = val.value("enabled", true);
        bbox.velocity = val.value("velocity", false);
        config.components[key] = bbox;
    }

    return config;
}

// Global pixel layout
LayoutPixels g_layoutPixels;

std::string ConfigLoader::computeLayout(const LayoutConfig& config,
                                        int canvasW,
                                        int canvasH,
                                        bool isInkMode,
                                        float inkAspectRatio,
                                        float cameraAspectRatio) {
    g_layoutPixels.components.clear();

    // default component layout
    for (const auto& [name, bbox] : config.components) {
        PixelRect rect;
        rect.x = static_cast<int>(bbox.x * canvasW) + bbox.px;
        rect.y = static_cast<int>(bbox.y * canvasH) + bbox.py;
        rect.width = std::max(0, static_cast<int>(bbox.w * canvasW) - 2 * bbox.px);
        rect.height = std::max(0, static_cast<int>(bbox.h * canvasH) - 2 * bbox.py);
        g_layoutPixels.components[name] = rect;
    }

    if (canvasW > 0 && canvasH > 0) {
        const float viewportAspectRatio = isInkMode
            ? (inkAspectRatio > 0.0f ? inkAspectRatio : 1.0f)
            : 0.0f;

        const std::string preset = config.preset.empty() ? "default" : config.preset;
        if (preset == "default") {
            applyDefaultPreset(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio);
        } else if (preset == "focused") {
            applyFocusedPreset(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio);
        } else if (preset == "viewport") {
            applyViewportPreset(config, canvasW, canvasH, viewportAspectRatio);
        } else if (preset == "gallery_single") {
            applyGalleryPreset(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio, 1);
        } else if (preset == "gallery_double") {
            applyGalleryPreset(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio, 2);
        } else if (preset == "gallery_quad") {
            applyGalleryPreset(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio, 4);
        } else if (preset == "legacy" && isInkMode) {
            applyDefaultPreset(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio);
        } else {
            applyDefaultPreset(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio);
        }
    }

    // Build JSON response
    json j;
    for (const auto& [name, rect] : g_layoutPixels.components) {
        j[name] = {
            {"x", rect.x},
            {"y", rect.y},
            {"width", rect.width},
            {"height", rect.height}
        };
    }
    for (const auto& [name, bbox] : config.components) {
        if (j.contains(name)) {
            j[name]["target"] = bbox.target;
            j[name]["enabled"] = bbox.enabled;
            j[name]["velocity"] = bbox.velocity;
        }
    }

    return j.dump();
}

ProjectionConfig ConfigLoader::loadProjectionConfig(const json& j) {
    ProjectionConfig config;
    config.overrelaxationCoefficient = j["overrelaxationCoefficient"];
    config.iterations = j["iterations"];
    config.autoScaleIterations = j["autoScaleIterations"];
    config.motionScaleIterations = j["motionScaleIterations"];
    config.motionCooldownFrames = j["motionCooldownFrames"];
    config.motionThreshold = j["motionThreshold"];
    return config;
}

VorticityConfig ConfigLoader::loadVorticityConfig(const json& j) {
    VorticityConfig config;
    config.enabled = j["enabled"];
    config.strength = j["strength"];
    config.lengthScale = j["lengthScale"];
    return config;
}

WindTunnelConfig ConfigLoader::loadWindTunnelConfig(const json& j) {
    WindTunnelConfig config;
    config.side = j["side"];
    config.startPosition = j["startPosition"];
    config.endPosition = j["endPosition"];
    config.velocity = j["velocity"];
    return config;
}

CircleConfig ConfigLoader::loadCircleConfig(const json& j) {
    CircleConfig config;
    config.radius = j["radius"];
    config.momentumTransferStrength = j["momentumTransferStrength"];
    config.momentumTransferRadius = j["momentumTransferRadius"];

    json zScaling = j["zScaling"];
    config.zMin = zScaling["zMin"];
    config.zMax = zScaling["zMax"];
    config.scaleMin = zScaling["scaleMin"];
    config.scaleMax = zScaling["scaleMax"];

    config.handSmoothingAlphaLow = j["handSmoothingAlphaLow"];
    config.handSmoothingAlphaHigh = j["handSmoothingAlphaHigh"];
    config.handSpeedThreshold = j["handSpeedThreshold"];
    config.momentumTransferDeadZone = j["momentumTransferDeadZone"];

    return config;
}

std::string ConfigLoader::readFile(const char* filename) {
#ifdef __EMSCRIPTEN__
    std::string path = std::string("/") + filename; // emscripten virtual FS root
#else
    std::string path = std::string("../") + filename; // assuming run from build/ or debug/
#endif
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "ERR opening file: " << path << std::endl;
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}