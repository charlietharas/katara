#ifndef SHADER_BUNDLE_H
#define SHADER_BUNDLE_H

#include <string>
#include <vector>

static constexpr int kViewTargetCount = 12;

struct ViewRegistryEntry {
    int index = 0;
    std::string name;
    std::string fnName;
    std::string source;
};

struct ShaderBundleResult {
    std::string wgsl;
    std::string error;
    bool ok() const { return error.empty(); }
};

std::string generateApplyRenderTargetDispatch(const std::vector<ViewRegistryEntry>& views);
std::string viewDrawFunctionName(int index);
std::string prefixViewFunctions(int index, const std::string& source);
ShaderBundleResult bundleFragmentShader(const std::vector<ViewRegistryEntry>& views);
bool loadBuiltinViewRegistry(std::vector<ViewRegistryEntry>& outViews, std::string& outError);
bool validateViewSource(int index, const std::string& source, std::string& outError);

#endif
