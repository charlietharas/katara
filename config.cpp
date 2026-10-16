#include "config.h"
#include "layout.h"
#include <fstream>
#include <iostream>
#include <sstream>

#define CONFIG_DIE(msg) do { \
    std::cerr << "CONFIG ERR: " << msg << std::endl; \
    exit(1); \
} while(0)

#define JSON_REQUIRED(dst, j, key) (dst) = (j)[key]
#define JSON_OPTIONAL(dst, j, key, defaultVal) (dst) = (j).value(key, defaultVal)

ComponentBBox loadComponentBBox(const json& val) {
    ComponentBBox bbox;
    JSON_REQUIRED(bbox.x, val, "x");
    JSON_REQUIRED(bbox.y, val, "y");
    JSON_REQUIRED(bbox.w, val, "w");
    JSON_REQUIRED(bbox.h, val, "h");
    JSON_REQUIRED(bbox.px, val, "px");
    JSON_REQUIRED(bbox.py, val, "py");
    JSON_OPTIONAL(bbox.rotation, val, "rotation", 0);
    bbox.viewportTarget = val.value("viewportTarget", val.value("target", 2));
    bbox.viewportVelocityViewEnabled = val.value("viewportVelocityViewEnabled", val.value("velocity", false));
    bbox.histogramEnabled = val.value("histogramEnabled", val.value("enabled", true));
    return bbox;
}

InputMode parseInputMode(const std::string& mode) {
    static const std::pair<const char*, InputMode> ENTRIES[] = {
        {"hand", InputMode::Hand},
        {"mouse_pull", InputMode::MousePull},
    };
    for (const auto& [name, value] : ENTRIES) {
        if (mode == name) {
            return value;
        }
    }
    CONFIG_DIE("invalid inputMode: " << mode << " (valid: hand, mouse_pull)");
}

std::string inputModeToString(InputMode mode) {
    static const std::pair<InputMode, const char*> ENTRIES[] = {
        {InputMode::Hand, "hand"},
        {InputMode::MousePull, "mouse_pull"},
    };
    for (const auto& [value, name] : ENTRIES) {
        if (mode == value) {
            return name;
        }
    }
    return "hand";
}

Config ConfigLoader::loadConfig(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            CONFIG_DIE("Cannot open config file: " << filename);
        }

        json j;
        file >> j;
        file.close();

        Config config;
        config.inputMode = parseInputMode(j.value("inputMode", "hand"));
        if (j.contains("hands")) {
            JSON_OPTIONAL(config.hands.left, j["hands"], "left", "full");
            JSON_OPTIONAL(config.hands.right, j["hands"], "right", "full");
        }
        config.pipeline = stringToPipelineType(j["pipeline"]);
        JSON_OPTIONAL(config.imagePath, j, "imagePath", "");
        config.window = loadWindowConfig(j["window"]);
        config.simulation = loadSimulationConfig(j["simulation"]);
        config.layout = loadLayoutConfig(j["layout"]);

        return config;
    } catch (const nlohmann::json::exception& e) {
        CONFIG_DIE(e.what() << "; ensure config.json has all required keys.");
    }
}

std::string ConfigLoader::readFile(const char* filename) {
#ifdef __EMSCRIPTEN__
    const std::string path = std::string("/") + filename;
#else
    const std::string path = filename;
#endif
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string ConfigLoader::computeLayout(const LayoutConfig& config,
                                        int canvasW,
                                        int canvasH,
                                        bool isInkMode,
                                        float inkAspectRatio,
                                        float cameraAspectRatio) {
    g_layoutPixels.components.clear();
    applyDefaultComponentLayout(config, canvasW, canvasH);

    if (canvasW > 0 && canvasH > 0) {
        const float viewportAspectRatio = isInkMode
            ? (inkAspectRatio > 0.0f ? inkAspectRatio : 1.0f)
            : 0.0f;
        applyLayoutPreset(config, canvasW, canvasH, viewportAspectRatio, cameraAspectRatio, isInkMode);
    }

    return layoutPixelsToJson(g_layoutPixels, config);
}

PipelineType ConfigLoader::stringToPipelineType(const std::string& type) {
    static const std::pair<const char*, PipelineType> ENTRIES[] = {
        {"device", PipelineType::GPU},
        {"hybrid", PipelineType::HYBRID},
    };
    for (const auto& [name, value] : ENTRIES) {
        if (type == name) {
            return value;
        }
    }
    CONFIG_DIE("invalid pipeline: " << type << " (valid: device, hybrid)");
}

WindowConfig ConfigLoader::loadWindowConfig(const json& j) {
    WindowConfig config;
    JSON_REQUIRED(config.baseSize, j, "baseSize");
    JSON_REQUIRED(config.defaultWidth, j, "defaultWidth");
    JSON_REQUIRED(config.defaultHeight, j, "defaultHeight");
    return config;
}

SimulationConfig ConfigLoader::loadSimulationConfig(const json& j) {
    SimulationConfig config;
    JSON_REQUIRED(config.resolution, j, "resolution");
    JSON_REQUIRED(config.timestep, j, "timestep");
    JSON_REQUIRED(config.edges, j, "edges");

    config.projection = loadProjectionConfig(j["projection"]);
    config.vorticity = loadVorticityConfig(j["vorticity"]);
    config.windTunnel = loadWindTunnelConfig(j["windTunnel"]);
    config.circle = loadCircleConfig(j["circle"]);

    return config;
}

LayoutConfig ConfigLoader::loadLayoutConfig(const json& j) {
    LayoutConfig config;
    JSON_REQUIRED(config.preset, j, "preset");
    JSON_OPTIONAL(config.camerasEnabled, j, "camerasEnabled", true);
    JSON_OPTIONAL(config.labelsEnabled, j, "labelsEnabled", true);
    JSON_OPTIONAL(config.buttonsEnabled, j, "buttonsEnabled", true);
    JSON_OPTIONAL(config.disableHistograms, j, "disableHistograms", false);
    JSON_OPTIONAL(config.velocityScale, j, "velocityScale", 0.01f);

    for (auto& [key, val] : j["components"].items()) {
        config.components[key] = loadComponentBBox(val);
    }

    return config;
}

ProjectionConfig ConfigLoader::loadProjectionConfig(const json& j) {
    ProjectionConfig config;
    JSON_REQUIRED(config.overrelaxationCoefficient, j, "overrelaxationCoefficient");
    JSON_REQUIRED(config.iterations, j, "iterations");
    return config;
}

VorticityConfig ConfigLoader::loadVorticityConfig(const json& j) {
    VorticityConfig config;
    JSON_REQUIRED(config.strength, j, "strength");
    JSON_REQUIRED(config.lengthScale, j, "lengthScale");
    if (j.contains("enabled") && !j["enabled"].get<bool>()) {
        config.strength = 0.0f;
    }
    return config;
}

WindTunnelConfig ConfigLoader::loadWindTunnelConfig(const json& j) {
    WindTunnelConfig config;
    JSON_REQUIRED(config.side, j, "side");
    JSON_REQUIRED(config.startPosition, j, "startPosition");
    JSON_REQUIRED(config.endPosition, j, "endPosition");
    JSON_REQUIRED(config.velocity, j, "velocity");
    return config;
}

CircleConfig ConfigLoader::loadCircleConfig(const json& j) {
    CircleConfig config;
    JSON_REQUIRED(config.radius, j, "radius");
    JSON_REQUIRED(config.momentumTransferStrength, j, "momentumTransferStrength");
    JSON_REQUIRED(config.momentumTransferRadius, j, "momentumTransferRadius");
    JSON_OPTIONAL(config.handSensitivity, j, "handSensitivity", 0.3f);

    const json& zScaling = j["zScaling"];
    JSON_REQUIRED(config.zMin, zScaling, "zMin");
    JSON_REQUIRED(config.zMax, zScaling, "zMax");
    config.zScaleMin = zScaling.value("zScaleMin", zScaling.value("scaleMin", 1.0f));
    config.zScaleMax = zScaling.value("zScaleMax", zScaling.value("scaleMax", 1.0f));

    return config;
}

LayoutPixels g_layoutPixels;
