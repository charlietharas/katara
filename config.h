#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include "json.hpp"
#include "circle_state.h"

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
    float radius = 0.1f;
    float momentumTransferStrength = 0.25f;
    float momentumTransferRadius = 1.0f;

    // z-coordinate scaling for depth-based radius
    float zMin = -0.1f;      // closest hand
    float zMax = 0.1f;       // farthest hand
    float scaleMin = 2.0f;   // max scale when closest
    float scaleMax = 0.5f;   // min scale when furthest

    // velocity-adaptive smoothing for hand jitter reduction
    float handSmoothingAlphaLow = 0.05f;
    float handSmoothingAlphaHigh = 0.5f;
    float handSpeedThreshold = 5.0f;
    float momentumTransferDeadZone = 1.0f;
};

enum class PipelineType {
    CPU, // "host"
    GPU, // "device"
    HYBRID // "hybrid"
};

struct SimulationConfig {
    int resolution = 100;
    float timestep = 1.0f / 60.0f;
    float gravity = 0.0f;
    float fluidDensity = 1000.0f;
    int edges = 15; // 4-bit mask: left(8), top(4), bottom(2), right(1)
    ProjectionConfig projection;
    VorticityConfig vorticity;
    WindTunnelConfig windTunnel;
    CircleConfig circle;
};

struct RenderingConfig {
    int target = 2; // 0=pressure, 1=smoke, 2=both, 3=ink
    bool showVelocityVectors = false;
    bool disableHistograms = false;
    float velocityScale = 0.05f;
};

struct InkConfig {
    std::string imagePath = "";
};

struct ComponentBBox {
    float x = 0.0f;     // normalized [0,1]
    float y = 0.0f;
    float w = 0.5f;
    float h = 0.5f;
    int px = 0;          // horizontal padding (raw px)
    int py = 0;          // vertical padding (raw px)
    int target = 2;      // render target (viewports: 0=pressure,1=smoke,2=both,3=ink)
    bool enabled = true; // enabled flag (histograms)
    bool velocity = false; // show velocity vectors (viewports)
};

struct PixelRect {
    int x = 0, y = 0, width = 0, height = 0;
};

struct LayoutConfig {
    std::map<std::string, ComponentBBox> components;
};

struct LayoutPixels {
    std::map<std::string, PixelRect> components;
};

extern LayoutPixels g_layoutPixels;

struct Config {
    PipelineType pipeline = PipelineType::CPU;
    WindowConfig window;
    SimulationConfig simulation;
    RenderingConfig rendering;
    InkConfig ink;
    LayoutConfig layout;  // layout configuration for flexible frontend
};

class ConfigLoader {
public:
    static Config loadConfig(const std::string& filename = "../config.json");
    static std::string readFile(const char* filename);

    // compute pixel layout from normalized config + canvas dimensions
    static std::string computeLayout(const LayoutConfig& config,
                                     int canvasW,
                                     int canvasH,
                                     bool isInkMode = false,
                                     float inkAspectRatio = 1.0f,
                                     float cameraAspectRatio = (4.0f / 3.0f));

private:
    static PipelineType stringToPipelineType(const std::string& type);
    static WindowConfig loadWindowConfig(const json& j);
    static SimulationConfig loadSimulationConfig(const json& j);
    static RenderingConfig loadRenderingConfig(const json& j);
    static InkConfig loadInkConfig(const json& j);
    static LayoutConfig loadLayoutConfig(const json& j);
    static ProjectionConfig loadProjectionConfig(const json& j);
    static VorticityConfig loadVorticityConfig(const json& j);
    static WindTunnelConfig loadWindTunnelConfig(const json& j);
    static CircleConfig loadCircleConfig(const json& j);
};

// Global config — single source of truth, update in-place for runtime reload
extern Config g_config;

#endif