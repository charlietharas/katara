#include "shader_bundle.h"
#include "config.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

using json = nlohmann::json;

namespace {

std::string readRequiredFile(const char* filename, std::string& outError) {
    const std::string content = ConfigLoader::readFile(filename);
    if (content.empty()) {
        outError = std::string("Failed to read shader file: ") + filename;
    }
    return content;
}

bool containsSubstring(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::vector<std::string> collectFunctionNames(const std::string& source) {
    std::vector<std::string> names;
    size_t pos = 0;
    while ((pos = source.find("fn ", pos)) != std::string::npos) {
        pos += 3;
        while (pos < source.size() && std::isspace(static_cast<unsigned char>(source[pos]))) {
            pos++;
        }
        const size_t start = pos;
        while (pos < source.size() && isIdentChar(source[pos])) {
            pos++;
        }
        if (pos > start) {
            names.push_back(source.substr(start, pos - start));
        }
    }
    return names;
}

void replaceFunctionCalls(std::string& text, const std::string& name, const std::string& replacement) {
    const std::string needle = name + "(";
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        if (pos > 0 && isIdentChar(text[pos - 1])) {
            pos += 1;
            continue;
        }
        text.replace(pos, name.size(), replacement);
        pos += replacement.size() + 1;
    }
}

} // namespace

std::string viewDrawFunctionName(int index) {
    return "view" + std::to_string(index) + "_draw";
}

std::string prefixViewFunctions(int index, const std::string& source) {
    const std::string scopePrefix = "view" + std::to_string(index) + "_";
    std::vector<std::string> names = collectFunctionNames(source);
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return a.size() > b.size();
    });

    std::string prefixed = source;
    for (const std::string& name : names) {
        const std::string scopedName = scopePrefix + name;
        const std::string defNeedle = "fn " + name + "(";
        size_t defPos = 0;
        while ((defPos = prefixed.find(defNeedle, defPos)) != std::string::npos) {
            prefixed.replace(defPos + 3, name.size(), scopedName);
            defPos += 3 + scopedName.size() + 1;
        }
        replaceFunctionCalls(prefixed, name, scopedName);
    }
    return prefixed;
}

std::string generateApplyRenderTargetDispatch(const std::vector<ViewRegistryEntry>& views) {
    std::ostringstream oss;
    oss << "fn applyRenderTarget(\n";
    oss << "    renderTarget: i32,\n";
    oss << "    texCoord: vec2<i32>,\n";
    oss << "    pressure: f32,\n";
    oss << "    density: f32,\n";
    oss << ") -> vec3<f32> {\n";

    for (const ViewRegistryEntry& view : views) {
        oss << "    if (renderTarget == " << view.index << ") {\n";
        oss << "        return " << viewDrawFunctionName(view.index) << "(texCoord, pressure, density);\n";
        oss << "    }\n";
    }

    oss << "    var color = mapValueToColor(pressure, uniforms.pressureMin, uniforms.pressureMax);\n";
    oss << "    color = color - density * vec3<f32>(1.0, 1.0, 1.0);\n";
    oss << "    return max(color, vec3<f32>(0.0, 0.0, 0.0));\n";
    oss << "}\n";
    return oss.str();
}

ShaderBundleResult bundleFragmentShader(const std::vector<ViewRegistryEntry>& views) {
    ShaderBundleResult result;
    if (static_cast<int>(views.size()) != kViewTargetCount) {
        result.error = "Expected " + std::to_string(kViewTargetCount) + " view targets, got " +
                       std::to_string(views.size());
        return result;
    }

    std::string fileError;
    const std::string uniformSource = readRequiredFile("fragment_uniform.wgsl", fileError);
    if (!fileError.empty()) {
        result.error = fileError;
        return result;
    }

    const std::string fragmentSource = readRequiredFile("fragment.wgsl", fileError);
    if (!fileError.empty()) {
        result.error = fileError;
        return result;
    }

    for (const ViewRegistryEntry& view : views) {
        std::string validationError;
        if (!validateViewSource(view.index, view.source, validationError)) {
            result.error = validationError;
            return result;
        }
    }

    std::ostringstream bundled;
    bundled << uniformSource << "\n";
    for (const ViewRegistryEntry& view : views) {
        bundled << prefixViewFunctions(view.index, view.source) << "\n";
    }
    bundled << generateApplyRenderTargetDispatch(views) << "\n";
    bundled << fragmentSource;

    result.wgsl = bundled.str();
    return result;
}

bool validateViewSource(int index, const std::string& source, std::string& outError) {
    if (index < 0 || index >= kViewTargetCount) {
        outError = "View target index out of range: " + std::to_string(index);
        return false;
    }

    if (!containsSubstring(source, "fn draw(")) {
        outError = "Target " + std::to_string(index) + ": expected entry function fn draw(";
        return false;
    }
    if (!containsSubstring(source, "texCoord: vec2<i32>") ||
        !containsSubstring(source, "pressure: f32") ||
        !containsSubstring(source, "density: f32") ||
        !containsSubstring(source, "-> vec3<f32>")) {
        outError = "Target " + std::to_string(index) +
                   ": expected signature fn draw(texCoord: vec2<i32>, pressure: f32, density: f32) -> vec3<f32>";
        return false;
    }

    static const char* forbidden[] = {
        "@group",
        "@binding",
        "struct UniformData",
        "fn fs_main",
    };
    for (const char* token : forbidden) {
        if (containsSubstring(source, token)) {
            outError = std::string("Target ") + std::to_string(index) + ": custom view must not contain " + token;
            return false;
        }
    }

    outError.clear();
    return true;
}

bool loadBuiltinViewRegistry(std::vector<ViewRegistryEntry>& outViews, std::string& outError) {
    outViews.clear();

    const std::string registryText = ConfigLoader::readFile("views/registry.json");
    if (registryText.empty()) {
        outError = "Failed to read views/registry.json";
        return false;
    }

    json registry;
    try {
        registry = json::parse(registryText);
    } catch (const json::exception& e) {
        outError = std::string("Failed to parse views/registry.json: ") + e.what();
        return false;
    }

    outViews.reserve(kViewTargetCount);
    for (int index = 0; index < kViewTargetCount; ++index) {
        const std::string key = std::to_string(index);
        if (!registry.contains(key)) {
            outError = "views/registry.json missing entry for index " + key;
            return false;
        }

        const json& entry = registry[key];
        const std::string fileName = entry.value("file", "");
        const std::string name = entry.value("name", key);
        if (fileName.empty()) {
            outError = "views/registry.json entry " + key + " missing file";
            return false;
        }

        const std::string filePath = "views/" + fileName;
        const std::string source = ConfigLoader::readFile(filePath.c_str());
        if (source.empty()) {
            outError = "Failed to read view file: " + filePath;
            return false;
        }

        ViewRegistryEntry view;
        view.index = index;
        view.name = name;
        view.fnName = viewDrawFunctionName(index);
        view.source = source;
        outViews.push_back(std::move(view));
    }

    std::sort(outViews.begin(), outViews.end(), [](const ViewRegistryEntry& a, const ViewRegistryEntry& b) {
        return a.index < b.index;
    });

    outError.clear();
    return true;
}
