#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include "json.hpp"

using json = nlohmann::json;

struct WindowConfig {
    int baseSize = 800;
    int defaultWidth = 1200;
    int defaultHeight = 800;
};

struct ProjectionConfig {
    float overrelaxationCoefficient = 1.9f;
    int iterations = 40;
};

struct VorticityConfig {
    bool enabled = true;
    float strength = 10.0f;
    float lengthScale = 5.0f;
};

struct WindTunnelConfig {
    int side = 0; // -1=disabled, 0=left, 1=top, 2=bottom, 3=right
    float startPosition = 0.45f;
    float endPosition = 0.55f;
    float velocity = 1.5f;
};

struct CircleConfig {
    int radius = 10;
    float momentumTransferCoeff = 0.25f;
    float momentumTransferRadius = 1.0f;
};

struct SimulationConfig {
    std::string type = "cpu"; // "cpu" or "gpu"
    int resolution = 100;
    float timestep = 1.0f / 60.0f;
    float gravity = 0.0f;
    float fluidDensity = 1000.0f;
    ProjectionConfig projection;
    VorticityConfig vorticity;
    WindTunnelConfig windTunnel;
    CircleConfig circle;
};

struct RenderingConfig {
    std::string type = "gpu"; // "cpu" or "gpu"
    int target = 2; // 0=pressure, 1=smoke, 2=both, 3=ink
    bool showVelocityVectors = false;
    bool disableHistograms = false;
    float velocityScale = 0.05f;
};

struct InkConfig {
    float mixingRate = 0.001f;
    float diffusionRate = 0.0001f;
    float pressureStrength = 0.1f;
    float temporalWeight = 0.95f;
    std::string imagePath = "";
};

struct Config {
    WindowConfig window;
    SimulationConfig simulation;
    RenderingConfig rendering;
    InkConfig ink;
};

class ConfigLoader {
public:
    static Config loadConfig(const std::string& filename = "../config.json");

private:
    static WindowConfig loadWindowConfig(const json& j);
    static SimulationConfig loadSimulationConfig(const json& j);
    static RenderingConfig loadRenderingConfig(const json& j);
    static InkConfig loadInkConfig(const json& j);
    static ProjectionConfig loadProjectionConfig(const json& j);
    static VorticityConfig loadVorticityConfig(const json& j);
    static WindTunnelConfig loadWindTunnelConfig(const json& j);
    static CircleConfig loadCircleConfig(const json& j);
};

#endif