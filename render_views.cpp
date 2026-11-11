#include "render_views.h"
#include "config.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
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

// draw() wrapper
constexpr const char* kViewDrawOpen =
    "fn draw(\n"
    "    texCoord: vec2<i32>,\n"
    "    pressureTexture: texture_2d<f32>,\n"
    "    densityTexture: texture_2d<f32>,\n"
    "    velocityTexture: texture_2d<f32>,\n"
    "    solidTexture: texture_2d<f32>,\n"
    "    inkTexture: texture_2d<f32>,\n"
    ") -> vec4<f32> {\n";
constexpr const char* kViewDrawClose = "\n}\n";
constexpr const char* kViewDrawMarker = "#draw";

constexpr const char* kViewDrawSignatureHint =
    "fn draw(texCoord: vec2<i32>, pressureTexture: texture_2d<f32>, "
    "densityTexture: texture_2d<f32>, velocityTexture: texture_2d<f32>, "
    "solidTexture: texture_2d<f32>, inkTexture: texture_2d<f32>) -> vec4<f32>";

std::string trimLeadingNewlines(std::string text) {
    size_t start = 0;
    while (start < text.size() && (text[start] == '\n' || text[start] == '\r')) {
        start++;
    }
    return text.substr(start);
}

int countLines(const std::string& text) {
    if (text.empty()) {
        return 0;
    }
    int lines = 0;
    for (char c : text) {
        if (c == '\n') {
            ++lines;
        }
    }
    if (text.back() != '\n') {
        ++lines;
    }
    return lines;
}

}

std::string viewDrawFunctionName(int index) {
    return "view" + std::to_string(index) + "_draw";
}

std::string materializeViewDrawSource(const std::string& fileSource,
                                      int* outHelperLines,
                                      int* outOpenLines) {
    auto setMap = [&](int helperLines, int openLines) {
        if (outHelperLines) {
            *outHelperLines = helperLines;
        }
        if (outOpenLines) {
            *outOpenLines = openLines;
        }
    };

    if (containsSubstring(fileSource, "fn draw(")) {
        setMap(0, 0);
        return fileSource;
    }

    const int openLines = countLines(kViewDrawOpen);
    const size_t markerPos = fileSource.find(kViewDrawMarker);
    if (markerPos == std::string::npos) {
        setMap(0, openLines);
        return std::string(kViewDrawOpen) + fileSource + kViewDrawClose;
    }

    const std::string helpers = fileSource.substr(0, markerPos);
    const std::string body = trimLeadingNewlines(
        fileSource.substr(markerPos + std::char_traits<char>::length(kViewDrawMarker))
    );
    setMap(countLines(helpers), openLines);
    return helpers + kViewDrawOpen + body + kViewDrawClose;
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
    oss << "    pressureTexture: texture_2d<f32>,\n";
    oss << "    densityTexture: texture_2d<f32>,\n";
    oss << "    velocityTexture: texture_2d<f32>,\n";
    oss << "    solidTexture: texture_2d<f32>,\n";
    oss << "    inkTexture: texture_2d<f32>,\n";
    oss << ") -> vec4<f32> {\n";

    const char* args =
        "(texCoord, pressureTexture, densityTexture, velocityTexture, solidTexture, inkTexture)";
    for (const ViewRegistryEntry& view : views) {
        oss << "    if (renderTarget == " << view.index << ") {\n";
        oss << "        return " << viewDrawFunctionName(view.index) << args << ";\n";
        oss << "    }\n";
    }

    // fallback
    oss << "    let checker = ((texCoord.x ^ texCoord.y) & 8) != 0;\n";
    oss << "    let errColor = select(vec3<f32>(1.0, 0.0, 1.0), vec3<f32>(0.0, 0.0, 0.0), checker);\n";
    oss << "    return vec4<f32>(errColor, 1.0);\n";
    oss << "}\n";
    return oss.str();
}

ShaderBundleResult bundleFragmentShader(const std::vector<ViewRegistryEntry>& views,
                                        bool divergenceBinomialFiltering) {
    ShaderBundleResult result;
    if (views.empty()) {
        result.error = "View registry is empty";
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
    int nextLine = 1;
    auto pushRegion = [&](const std::string& chunk, const char* regionName,
                          int helperLines = 0, int openLines = 0) {
        if (result.regionCount >= kMaxBundleRegions) {
            bundled << chunk;
            return;
        }
        BundleLineRegion& region = result.regions[result.regionCount++];
        region.endLine = nextLine + countLines(chunk);
        region.helperLines = helperLines;
        region.openLines = openLines;
        std::snprintf(region.name, sizeof(region.name), "%s", regionName);
        nextLine = region.endLine;
        bundled << chunk;
    };

    pushRegion(uniformSource + "\n", "fragment_uniform.wgsl");
    pushRegion(
        std::string("const DIVERGENCE_BINOMIAL_FILTERING: bool = ")
            + (divergenceBinomialFiltering ? "true" : "false") + ";\n",
        "fragment_uniform.wgsl"
    );
    for (const ViewRegistryEntry& view : views) {
        const std::string label = !view.file.empty() ? view.file : view.name;
        pushRegion(
            prefixViewFunctions(view.index, view.source) + "\n",
            label.c_str(),
            view.errHelperLines,
            view.errOpenLines
        );
    }
    pushRegion(generateApplyRenderTargetDispatch(views) + "\n", "dispatch");
    pushRegion(fragmentSource, "fragment.wgsl");

    result.wgsl = bundled.str();
    return result;
}

bool validateViewSource(int index, const std::string& source, std::string& outError) {
    if (index < 0) {
        outError = "View target index out of range: " + std::to_string(index);
        return false;
    }

    if (!containsSubstring(source, "fn draw(")) {
        outError = "Target " + std::to_string(index) + ": expected entry function fn draw(";
        return false;
    }
    if (!containsSubstring(source, "texCoord: vec2<i32>") ||
        !containsSubstring(source, "pressureTexture: texture_2d<f32>") ||
        !containsSubstring(source, "densityTexture: texture_2d<f32>") ||
        !containsSubstring(source, "velocityTexture: texture_2d<f32>") ||
        !containsSubstring(source, "solidTexture: texture_2d<f32>") ||
        !containsSubstring(source, "inkTexture: texture_2d<f32>") ||
        !containsSubstring(source, "-> vec4<f32>")) {
        outError = std::string("Target ") + std::to_string(index) +
                   ": expected signature " + kViewDrawSignatureHint;
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

    if (!registry.contains("count") || !registry["count"].is_number_integer()) {
        outError = "views/registry.json missing integer count";
        return false;
    }

    const int count = registry["count"].get<int>();
    if (count <= 0) {
        outError = "views/registry.json count must be positive";
        return false;
    }

    outViews.reserve(count);
    for (int index = 0; index < count; ++index) {
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
        const std::string fileSource = ConfigLoader::readFile(filePath.c_str());
        if (fileSource.empty()) {
            outError = "Failed to read view file: " + filePath;
            return false;
        }

        ViewRegistryEntry view;
        view.index = index;
        view.name = name;
        view.file = fileName;
        view.fnName = viewDrawFunctionName(index);
        view.source = materializeViewDrawSource(
            fileSource, &view.errHelperLines, &view.errOpenLines
        );
        outViews.push_back(std::move(view));
    }

    std::sort(outViews.begin(), outViews.end(), [](const ViewRegistryEntry& a, const ViewRegistryEntry& b) {
        return a.index < b.index;
    });

    outError.clear();
    return true;
}
