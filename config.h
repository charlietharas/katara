#ifndef CONFIG_H
#define CONFIG_H

#include <map>
#include <string>
#include "json.hpp"

using json = nlohmann::json;

enum class InputMode : int {
    Hand = 0,
    MousePull = 1,
};


struct HandsConfig {
    std::string left = "full";
    std::string right = "full";
};


enum class PipelineType {
    GPU,    // "device"
    HYBRID, // "hybrid"
};


struct WindowConfig {
    int baseSize;
    int defaultWidth;
    int defaultHeight;
};


struct ProjectionConfig {
    float overrelaxationCoefficient;
    int iterations;
};


struct VorticityConfig {
    float strength;
    float lengthScale;
};


struct WindTunnelConfig {
    int side;            // -1=disabled, 0=left, 1=top, 2=bottom, 3=right
    float startPosition; // 0-1
    float endPosition;
    float velocity;
};


struct CircleConfig {
    float radius;
    float momentumTransferStrength;
    float momentumTransferRadius;
    float handSensitivity; // 0-1 low-high

    // depth scaling
    float zMin;        // closest hand
    float zMax;        // farthest hand
    float zScaleMin;   // max scale when closest
    float zScaleMax;   // min scale when furthest
};


struct SimulationConfig {
    int resolution;
    float timestep;
    int edges; // 4-bit mask: left(8), top(4), bottom(2), right(1)
    ProjectionConfig projection;
    VorticityConfig vorticity;
    WindTunnelConfig windTunnel;
    CircleConfig circle;
};


struct ComponentBBox {
    float x;
    float y;
    float w;
    float h;
    int px;
    int py;
    int rotation = 0;
    int viewportTarget;
    bool viewportVelocityViewEnabled;
    bool histogramEnabled;
};


struct LayoutConfig {
    std::string preset;
    bool labelsEnabled = true;
    bool buttonsEnabled = true;
    bool camerasEnabled = true;
    bool disableHistograms = false;
    float velocityScale = 0.01f;
    std::map<std::string, ComponentBBox> components;
};


struct PixelRect {
    int x, y, width, height;
};

inline PixelRect makeRect(int x, int y, int width, int height) {
    return {x, y, width, height};
}

inline constexpr const char LAYOUT_PRESET_DEFAULT[] = "default";
inline constexpr const char LAYOUT_PRESET_FOCUSED[] = "focused";
inline constexpr const char LAYOUT_PRESET_VIEWPORT[] = "viewport";
inline constexpr const char LAYOUT_PRESET_GALLERY_SINGLE[] = "gallery_single";
inline constexpr const char LAYOUT_PRESET_GALLERY_DOUBLE[] = "gallery_double";
inline constexpr const char LAYOUT_PRESET_GALLERY_QUAD[] = "gallery_quad";
inline constexpr const char LAYOUT_PRESET_LEGACY[] = "legacy";

inline constexpr const char* const LAYOUT_PRESETS[] = {
    LAYOUT_PRESET_DEFAULT,
    LAYOUT_PRESET_FOCUSED,
    LAYOUT_PRESET_VIEWPORT,
    LAYOUT_PRESET_GALLERY_SINGLE,
    LAYOUT_PRESET_GALLERY_DOUBLE,
    LAYOUT_PRESET_GALLERY_QUAD,
};
inline constexpr int NUM_LAYOUT_PRESETS = static_cast<int>(sizeof(LAYOUT_PRESETS) / sizeof(LAYOUT_PRESETS[0]));

struct LayoutPixels {
    std::map<std::string, PixelRect> components;
};


struct Config {
    InputMode inputMode = InputMode::Hand;
    HandsConfig hands;
    PipelineType pipeline;
    std::string imagePath;
    WindowConfig window;
    SimulationConfig simulation;
    LayoutConfig layout;
};

// helpers
inline bool isMouseInput(InputMode mode) {
    return mode == InputMode::MousePull;
}

inline int inputModeToInt(InputMode mode) {
    return static_cast<int>(mode);
}

InputMode parseInputMode(const std::string& mode);
std::string inputModeToString(InputMode mode);


inline bool layoutHasInkViewport(const LayoutConfig& layout) {
    for (const auto& [name, bbox] : layout.components) {
        if (name.rfind("viewport_", 0) == 0 && bbox.viewportTarget == 3) {
            return true;
        }
    }
    return false;
}

inline bool configUsesInkAspect(const Config& config) {
    return !config.imagePath.empty();
}


class ConfigLoader {
public:
    static Config loadConfig(const std::string& filename);
    static std::string readFile(const char* filename);

    static std::string computeLayout(const LayoutConfig& config,
                                     int canvasW,
                                     int canvasH,
                                     bool isInkMode,
                                     float inkAspectRatio,
                                     float cameraAspectRatio);

private:
    static PipelineType stringToPipelineType(const std::string& type);
    static WindowConfig loadWindowConfig(const json& j);
    static SimulationConfig loadSimulationConfig(const json& j);
    static LayoutConfig loadLayoutConfig(const json& j);
    static ProjectionConfig loadProjectionConfig(const json& j);
    static VorticityConfig loadVorticityConfig(const json& j);
    static WindTunnelConfig loadWindTunnelConfig(const json& j);
    static CircleConfig loadCircleConfig(const json& j);
};

extern LayoutPixels g_layoutPixels;
extern Config g_config;

#endif
