#include "config.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

using json = nlohmann::json;

Config ConfigLoader::loadConfig(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: could not open config file: " << filename << std::endl;
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
    config.radius = j.value("radius", 10);
    config.momentumTransferCoeff = j.value("momentumTransferCoeff", 0.25f);
    config.momentumTransferRadius = j.value("momentumTransferRadius", 1.0f);
    return config;
}

std::string ConfigLoader::readFile(const char* filename) {
    std::string path = std::string("../") + filename; // NOTE assuming run from build/ or debug/ !
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: could not open file: " << path << std::endl;
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}