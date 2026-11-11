#ifndef RENDER_VIEWS_H
#define RENDER_VIEWS_H

#include <string>
#include <vector>

struct ViewRegistryEntry {
    int index = 0;
    std::string name;
    std::string file;
    std::string fnName;
    std::string source;
    int errHelperLines = 0;
    int errOpenLines = 0;
};

static constexpr int kMaxBundleRegions = 32;
static constexpr int kBundleRegionNameLen = 40;

struct BundleLineRegion {
    int endLine = 0;
    int helperLines = 0;
    int openLines = 0;
    char name[kBundleRegionNameLen] = {};

    int toFileLine(int localLine) const {
        if (openLines <= 0 || localLine <= helperLines) {
            return localLine;
        }
        if (localLine <= helperLines + openLines) {
            return helperLines + 1;
        }
        return localLine - (openLines - 1);
    }
};

struct ShaderBundleResult {
    std::string wgsl;
    std::string error;
    BundleLineRegion regions[kMaxBundleRegions] = {};
    int regionCount = 0;
    bool ok() const { return error.empty(); }
};

std::string generateApplyRenderTargetDispatch(const std::vector<ViewRegistryEntry>& views);
std::string viewDrawFunctionName(int index);
std::string prefixViewFunctions(int index, const std::string& source);
std::string materializeViewDrawSource(const std::string& fileSource,
                                      int* outHelperLines = nullptr,
                                      int* outOpenLines = nullptr);
ShaderBundleResult bundleFragmentShader(const std::vector<ViewRegistryEntry>& views,
                                        bool divergenceBinomialFiltering = true);
bool loadBuiltinViewRegistry(std::vector<ViewRegistryEntry>& outViews, std::string& outError);
bool validateViewSource(int index, const std::string& source, std::string& outError);

#endif
