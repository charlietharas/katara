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
    int baseSize;
    int defaultWidth;
    int defaultHeight;
};

struct ProjectionConfig {
    float overrelaxationCoefficient;
    int iterations;

    // Auto-scaling parameters
    bool autoScaleIterations;
    int motionScaleIterations;
    int motionCooldownFrames;
    float motionThreshold;
};

struct VorticityConfig {
    bool enabled;
    float strength;
    float lengthScale;
};

struct WindTunnelConfig {
    int side; // -1=disabled, 0=left, 1=top, 2=bottom, 3=right
    float startPosition;
    float endPosition;
    float velocity;
};

struct CircleConfig {
    float radius;
    float momentumTransferStrength;
    float momentumTransferRadius;

    // z-coordinate scaling for depth-based radius
    float zMin;      // closest hand
    float zMax;       // farthest hand
    float scaleMin;   // max scale when closest
    float scaleMax;   // min scale when furthest

    // velocity-adaptive smoothing for hand jitter reduction
    float handSmoothingAlphaLow;
    float handSmoothingAlphaHigh;
    float handSpeedThreshold;
    float momentumTransferDeadZone;
};

enum class PipelineType {
    CPU, // "host"
    GPU, // "device"
    HYBRID // "hybrid"
};

struct SimulationConfig {
    int resolution;
    float timestep;
    float gravity;
    float fluidDensity;
    int edges; // 4-bit mask: left(8), top(4), bottom(2), right(1)
    ProjectionConfig projection;
    VorticityConfig vorticity;
    WindTunnelConfig windTunnel;
    CircleConfig circle;
};

struct RenderingConfig {
    // 0=pressure, 1=smoke, 2=both(pretty), 3=ink, 4=divergence, 5=heatmap, 6=normals, 7=threshold+bloom
    int target;
    bool showVelocityVectors;
    bool disableHistograms;
    float velocityScale;
};

struct InkConfig {
    std::string imagePath;
};

struct ComponentBBox {
    float x;     // normalized [0,1]
    float y;
    float w;
    float h;
    int px;          // horizontal padding (raw px)
    int py;          // vertical padding (raw px)
    // render target (viewports: 0=pressure,1=smoke,2=both,3=ink,4=divergence,5=heatmap,6=normals,7=threshold+bloom)
    int target;
    bool enabled; // enabled flag (histograms)
    bool velocity; // show velocity vectors (viewports)
};

struct PixelRect {
    int x, y, width, height;
};

struct LayoutConfig {
    std::string preset;
    bool camerasEnabled = true;
    std::map<std::string, ComponentBBox> components;
};

struct LayoutPixels {
    std::map<std::string, PixelRect> components;
};

extern LayoutPixels g_layoutPixels;

struct Config {
    PipelineType pipeline;
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