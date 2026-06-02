#include "config.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>

using json = nlohmann::json;

namespace {
bool hasPrefix(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

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
}  // namespace

Config ConfigLoader::loadConfig(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "ERR opening config file: " << filename << std::endl;
        exit(1);
    }

    json j;
    file >> j;
    file.close();

    Config config;

    if (j.contains("pipeline")) {
        config.pipeline = stringToPipelineType(j["pipeline"]);
    }   
    if (j.contains("window")) {
        config.window = loadWindowConfig(j["window"]);
    }
    if (j.contains("simulation")) {
        config.simulation = loadSimulationConfig(j["simulation"]);
    }
    if (j.contains("rendering")) {
        config.rendering = loadRenderingConfig(j["rendering"]);
    }
    if (j.contains("ink")) {
        config.ink = loadInkConfig(j["ink"]);
    }
    if (j.contains("layout")) {
        config.layout = loadLayoutConfig(j["layout"]);
    }

    return config;
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
    config.baseSize = j.value("baseSize", 800);
    config.defaultWidth = j.value("defaultWidth", 1200);
    config.defaultHeight = j.value("defaultHeight", 800);
    return config;
}

SimulationConfig ConfigLoader::loadSimulationConfig(const json& j) {
    SimulationConfig config;
    config.resolution = j.value("resolution", 100);
    config.timestep = j.value("timestep", 1.0f / 60.0f);
    config.gravity = j.value("gravity", 0.0f);
    config.fluidDensity = j.value("fluidDensity", 1000.0f);
    config.edges = j.value("edges", 15);

    if (j.contains("projection")) {
        config.projection = loadProjectionConfig(j["projection"]);
    }
    if (j.contains("vorticity")) {
        config.vorticity = loadVorticityConfig(j["vorticity"]);
    }
    if (j.contains("windTunnel")) {
        config.windTunnel = loadWindTunnelConfig(j["windTunnel"]);
    }
    if (j.contains("circle")) {
        config.circle = loadCircleConfig(j["circle"]);
    }

    return config;
}

RenderingConfig ConfigLoader::loadRenderingConfig(const json& j) {
    RenderingConfig config;
    config.target = j.value("target", 2);
    config.showVelocityVectors = j.value("showVelocityVectors", false);
    config.disableHistograms = j.value("disableHistograms", false);
    config.velocityScale = j.value("velocityScale", 0.05f);
    return config;
}

InkConfig ConfigLoader::loadInkConfig(const json& j) {
    InkConfig config;
    config.imagePath = j.value("imagePath", "");
    return config;
}

LayoutConfig ConfigLoader::loadLayoutConfig(const json& j) {
    LayoutConfig config;

    if (j.contains("components")) {
        for (auto& [key, val] : j["components"].items()) {
            ComponentBBox bbox;
            bbox.x = val.value("x", 0.0f);
            bbox.y = val.value("y", 0.0f);
            bbox.w = val.value("w", 0.5f);
            bbox.h = val.value("h", 0.5f);
            bbox.px = val.value("px", 0);
            bbox.py = val.value("py", 0);
            bbox.target = val.value("target", 2);
            bbox.enabled = val.value("enabled", true);
            config.components[key] = bbox;
        }
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

    if (isInkMode && canvasW > 0 && canvasH > 0) {
        const float viewportAspectRatio = inkAspectRatio > 0.0f ? inkAspectRatio : 1.0f;
        const float safeCameraAspectRatio = cameraAspectRatio > 0.0f ? cameraAspectRatio : (4.0f / 3.0f);

        // quadtree layout matches image aspect ratio
        int cellWidth = 0;
        int cellHeight = 0;
        const float canvasAspectRatio = static_cast<float>(canvasW) / static_cast<float>(canvasH);
        if (canvasAspectRatio > viewportAspectRatio) {
            cellHeight = canvasH / 2;
            cellWidth = std::max(0, static_cast<int>(cellHeight * viewportAspectRatio));
        } else {
            cellWidth = canvasW / 2;
            cellHeight = std::max(0, static_cast<int>(cellWidth / viewportAspectRatio));
        }

        const int quadtreeWidth = cellWidth * 2;
        const int quadtreeHeight = cellHeight * 2;
        const int quadtreeX = (canvasW - quadtreeWidth) / 2;
        const int quadtreeY = (canvasH - quadtreeHeight) / 2;

        PixelRect topLeft = {quadtreeX, quadtreeY, cellWidth, cellHeight};
        PixelRect topRight = {quadtreeX + cellWidth, quadtreeY, cellWidth, cellHeight};
        PixelRect bottomLeft = {quadtreeX, quadtreeY + cellHeight, cellWidth, cellHeight};
        PixelRect bottomRight = {quadtreeX + cellWidth, quadtreeY + cellHeight, cellWidth, cellHeight};

        auto applyViewport = [&](const std::string& name, const PixelRect& quadrant) {
            auto cfgIt = config.components.find(name);
            if (cfgIt == config.components.end()) {
                return;
            }
            PixelRect fitted = fitRectToAspectCentered(quadrant, viewportAspectRatio);
            g_layoutPixels.components[name] = insetRect(fitted, cfgIt->second.px, cfgIt->second.py);
        };

        // viewports
        applyViewport("viewport_1", topRight);
        applyViewport("viewport_2", bottomRight);
        applyViewport("viewport_3", bottomLeft);

        // top-left quadrant contains the funky stuff
        const float histogramAspectRatio = 1.5f;
        std::vector<std::string> histogramNames;
        if (config.components.find("density_histogram") != config.components.end()) {
            histogramNames.push_back("density_histogram");
        }
        if (config.components.find("velocity_histogram") != config.components.end()) {
            histogramNames.push_back("velocity_histogram");
        }
        if (config.components.find("entropy_time_series") != config.components.end()) {
            histogramNames.push_back("entropy_time_series");
        }

        int histogramBandHeight = 0;
        if (!histogramNames.empty()) {
            const int histogramCount = static_cast<int>(histogramNames.size());
            const int slotWidth = topLeft.width / std::max(1, histogramCount);
            int eachHeight = std::max(0, static_cast<int>(slotWidth / histogramAspectRatio));
            const int maxBandHeight = std::max(0, topLeft.height / 2);
            if (eachHeight > maxBandHeight) {
                eachHeight = maxBandHeight;
            }
            const int eachWidth = std::min(slotWidth, std::max(0, static_cast<int>(eachHeight * histogramAspectRatio)));
            const int startX = topLeft.x;
            const int startY = topLeft.y;
            histogramBandHeight = eachHeight;
            const int rightEdge = startX + topLeft.width;
            for (int i = 0; i < histogramCount; i++) {
                const std::string& name = histogramNames[histogramCount - 1 - i];
                const auto& cfg = config.components.at(name);
                const int histX = rightEdge - ((i + 1) * eachWidth);
                PixelRect base = {histX, startY, eachWidth, eachHeight};
                g_layoutPixels.components[name] = insetRect(base, cfg.px, cfg.py);
            }
        }

        auto cameraIt = config.components.find("camera_frame");
        if (cameraIt != config.components.end()) {
            PixelRect availableCameraArea = topLeft;
            if (histogramBandHeight > 0) {
                availableCameraArea.y += histogramBandHeight;
                availableCameraArea.height = std::max(0, availableCameraArea.height - histogramBandHeight);
            }

            PixelRect fitted = fitRectToAspectBottomRight(availableCameraArea, safeCameraAspectRatio);
            g_layoutPixels.components["camera_frame"] = insetRect(fitted, cameraIt->second.px, cameraIt->second.py);
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
        }
    }

    return j.dump();
}

ProjectionConfig ConfigLoader::loadProjectionConfig(const json& j) {
    ProjectionConfig config;
    config.overrelaxationCoefficient = j.value("overrelaxationCoefficient", 1.9f);
    config.iterations = j.value("iterations", 40);
    return config;
}

VorticityConfig ConfigLoader::loadVorticityConfig(const json& j) {
    VorticityConfig config;
    config.enabled = j.value("enabled", true);
    config.strength = j.value("strength", 10.0f);
    config.lengthScale = j.value("lengthScale", 5.0f);
    return config;
}

WindTunnelConfig ConfigLoader::loadWindTunnelConfig(const json& j) {
    WindTunnelConfig config;
    config.side = j.value("side", 0);
    config.startPosition = j.value("startPosition", 0.45f);
    config.endPosition = j.value("endPosition", 0.55f);
    config.velocity = j.value("velocity", 1.5f);
    return config;
}

CircleConfig ConfigLoader::loadCircleConfig(const json& j) {
    CircleConfig config;
    config.radius = j.value("radius", 0.1f);
    config.momentumTransferStrength = j.value("momentumTransferStrength", 0.25f);
    config.momentumTransferRadius = j.value("momentumTransferRadius", 1.0f);

    if (j.contains("zScaling")) {
        json zScaling = j["zScaling"];
        config.zMin = zScaling.value("zMin", -0.1f);
        config.zMax = zScaling.value("zMax", 0.1f);
        config.scaleMin = zScaling.value("scaleMin", 2.0f);
        config.scaleMax = zScaling.value("scaleMax", 0.5f);
    }

    config.handSmoothingAlphaLow = j.value("handSmoothingAlphaLow", 0.05f);
    config.handSmoothingAlphaHigh = j.value("handSmoothingAlphaHigh", 0.5f);
    config.handSpeedThreshold = j.value("handSpeedThreshold", 5.0f);
    config.momentumTransferDeadZone = j.value("momentumTransferDeadZone", 1.0f);

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