#ifndef WGPU_BOILERPLATE_H
#define WGPU_BOILERPLATE_H

#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include "config.h"

// Compatibility layer for emdawnwebgpu API differences
#ifdef WEBGPU_BACKEND_EMDAWNWEBGPU
typedef WGPUTextureUsage WGPUTextureUsageFlags;
typedef WGPUBufferUsage WGPUBufferUsageFlags;
typedef WGPUTexelCopyTextureInfo WGPUImageCopyTexture;
typedef WGPUTexelCopyBufferLayout WGPUTextureDataLayout;
inline WGPUStringView wgpuStr(const char* s) {
    return WGPUStringView{ s, s ? WGPU_STRLEN : 0 };
}
#define WGPU_CSTR(s) wgpuStr(s)
#else
#define WGPU_CSTR(s) (s)
#endif

// MACROS
#define RETURN_FALSE_IF_FAIL(fn) if (!(fn)) { return false; }

#define DECLARE_STORAGE_VIEW(name) WGPUTextureView name##TextureStorageView = nullptr;

#define DECLARE_TEXTURE_AND_VIEW(name) \
    WGPUTexture name##Texture = nullptr; \
    WGPUTextureView name##TextureView = nullptr;

#define DECLARE_PIPELINE_RESOURCES(name) \
    WGPUPipelineLayout name##PipelineLayout = nullptr; \
    WGPUBindGroupLayout name##BindGroupLayout = nullptr; \
    WGPUBindGroup name##BindGroup = nullptr; \
    WGPUComputePipeline name##Pipeline = nullptr;

#define CPU_SIM_GETTER(name) \
    const std::vector<float>& name() const override { return cpuSimulator.name(); }

#define CREATE_STORAGE_TEXTURE(name, format) \
    do { \
        TextureDesc desc = {#name, WGPUTextureFormat_##format, \
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | \
            WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst}; \
        if (!createTexture(desc, name##Texture, name##TextureView)) { \
            return false; \
        } \
    } while(0)

#define RELEASE_TEXTURE_VIEW(name) \
    releaseResource(name##TextureView, wgpuTextureViewRelease);

#define RELEASE_TEXTURE(name) \
    releaseResource(name##Texture, wgpuTextureRelease); \
    releaseResource(name##TextureView, wgpuTextureViewRelease);

#define RELEASE_TEXTURE_WITH_STORAGE(name) \
    releaseResource(name##TextureView, wgpuTextureViewRelease); \
    releaseResource(name##TextureStorageView, wgpuTextureViewRelease); \
    releaseResource(name##Texture, wgpuTextureRelease);

#define RELEASE_PIPELINE_RESOURCES(name) \
    releaseResource(name##Pipeline, wgpuComputePipelineRelease); \
    releaseResource(name##BindGroup, wgpuBindGroupRelease); \
    releaseResource(name##BindGroupLayout, wgpuBindGroupLayoutRelease); \
    releaseResource(name##PipelineLayout, wgpuPipelineLayoutRelease);

#define RELEASE_BUFFER(name) \
    releaseResource(name, wgpuBufferRelease);

#define RELEASE_RENDER_PIPELINE(name) \
    releaseResource(name, wgpuRenderPipelineRelease);

#define RELEASE_COMPUTE_PIPELINE(name) \
    releaseResource(name, wgpuComputePipelineRelease);

#define RELEASE_BIND_GROUP(name) \
    releaseResource(name, wgpuBindGroupRelease);

#define RELEASE_BIND_GROUP_LAYOUT(name) \
    releaseResource(name, wgpuBindGroupLayoutRelease);

#define RELEASE_SAMPLER(name) \
    releaseResource(name, wgpuSamplerRelease);

#define RELEASE_HISTOGRAM_SLOT_RESOURCES(slot)                             \
    releaseResource((slot).minMaxBuffer, wgpuBufferRelease);               \
    releaseResource((slot).minMaxStagingBuffer, wgpuBufferRelease);        \
    releaseResource((slot).histogramBinBuffer, wgpuBufferRelease);         \
    releaseResource((slot).histogramStagingBuffer, wgpuBufferRelease);     \
    releaseResource((slot).minMaxUniformBuffer, wgpuBufferRelease);        \
    releaseResource((slot).bindGroupMinMax, wgpuBindGroupRelease);         \
    releaseResource((slot).bindGroupHistogramBins, wgpuBindGroupRelease);

// TEXTURE DESCRIPTOR (used downstream in createTexture, which is implemented differently for sim/render)
struct TextureDesc {
    const char* name;
    WGPUTextureFormat format;
    WGPUTextureUsageFlags usage;
};

// DATA TRANSFER HELPERS
static uint32_t floatToOrderedUint(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    if (bits & 0x80000000u) {
        return ~bits;
    }
    return bits ^ 0x80000000u;
}

static float orderedUintToFloat(uint32_t ordered) {
    uint32_t bits = (ordered & 0x80000000u) ? (ordered ^ 0x80000000u) : ~ordered;
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// render and sim_gpu both inherit from this
// which provides lots of boilerplate helpers *that are reused between both classes*
// if a helper is not reused between render/sim, it is implemented in the respective class
class WGPUBoilerplate {
public:
    virtual ~WGPUBoilerplate() = default;

    // webgpu core
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;

    // load code from shaders
    WGPUShaderModule createShaderModuleFromSource(const std::string& shaderSource) {
        if (!device || shaderSource.empty()) {
            return nullptr;
        }

#ifdef WEBGPU_BACKEND_EMDAWNWEBGPU
        WGPUShaderSourceWGSL wgslSource = {};
        wgslSource.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslSource.code = WGPU_CSTR(shaderSource.c_str());

        WGPUShaderModuleDescriptor shaderDesc = {};
        shaderDesc.nextInChain = &wgslSource.chain;
#else
        WGPUShaderModuleWGSLDescriptor wgslDesc = {};
        wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
        wgslDesc.code = shaderSource.c_str();

        WGPUShaderModuleDescriptor shaderDesc = {};
        shaderDesc.nextInChain = const_cast<WGPUChainedStruct*>(&wgslDesc.chain);
#endif
        WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
        return shaderModule;
    }

    WGPUShaderModule createShaderModule(const char* shaderFile) {
        if (!device) {
            return nullptr;
        }

        std::string shaderSource = ConfigLoader::readFile(shaderFile);
        if (shaderSource.empty()) {
            return nullptr;
        }

        return createShaderModuleFromSource(shaderSource);
    }

    // pipeline resource factories
    WGPUBindGroupLayout createBindGroupLayout(int count, WGPUBindGroupLayoutEntry* entries) {
        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = count;
        layoutDesc.entries = entries;
        return wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
    }

    WGPUPipelineLayout createPipelineLayout(WGPUBindGroupLayout* layouts, int count = 1) {
        WGPUPipelineLayoutDescriptor pipelineDesc = {};
        pipelineDesc.bindGroupLayoutCount = count;
        pipelineDesc.bindGroupLayouts = layouts;
        return wgpuDeviceCreatePipelineLayout(device, &pipelineDesc);
    }

    WGPUBindGroup createBindGroup(int count, WGPUBindGroupEntry* entries, WGPUBindGroupLayout layout) {
        WGPUBindGroupDescriptor bindGroupDesc = {};
        bindGroupDesc.entryCount = count;
        bindGroupDesc.entries = entries;
        bindGroupDesc.layout = layout;
        return wgpuDeviceCreateBindGroup(device, &bindGroupDesc);
    }

    // texture view factory (used in downstream createTexture() implementations)
    WGPUTexture createTextureView(int gridX, int gridY, WGPUTextureFormat format, WGPUTextureUsageFlags usage, WGPUTextureView& outView) {
        WGPUTextureDescriptor texDesc = {};
        texDesc.size = {static_cast<uint32_t>(gridX), static_cast<uint32_t>(gridY), 1};
        texDesc.format = format;
        texDesc.usage = usage;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;

        WGPUTexture texture = wgpuDeviceCreateTexture(device, &texDesc);
        if (!texture) {
            return nullptr;
        }

        outView = wgpuTextureCreateView(texture, nullptr);
        if (!outView) {
            wgpuTextureRelease(texture);
            return nullptr;
        }

        return texture;
    }

    // layout entry factories
    WGPUBindGroupLayoutEntry createSampleTextureLayoutEntry(int binding, WGPUShaderStage visibility) {
        WGPUBindGroupLayoutEntry entry = {};
        entry.binding = binding;
        entry.visibility = visibility;
        entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        entry.texture.viewDimension = WGPUTextureViewDimension_2D;
        entry.texture.multisampled = false;
        return entry;
    }

    WGPUBindGroupLayoutEntry createUniformBufferLayoutEntry(int binding, size_t minSize) {
        WGPUBindGroupLayoutEntry entry = {};
        entry.binding = binding;
        entry.visibility = WGPUShaderStage_Compute | WGPUShaderStage_Fragment;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.hasDynamicOffset = false;
        entry.buffer.minBindingSize = minSize;
        return entry;
    }

    // bind group entry factories
    WGPUBindGroupEntry createUniformBufferBindGroupEntry(int binding, WGPUBuffer buffer, size_t size) {
        WGPUBindGroupEntry entry = {};
        entry.binding = binding;
        entry.buffer = buffer;
        entry.offset = 0;
        entry.size = size;
        return entry;
    }

    WGPUBindGroupEntry createTextureViewBindGroupEntry(int binding, WGPUTextureView view) {
        WGPUBindGroupEntry entry = {};
        entry.binding = binding;
        entry.textureView = view;
        return entry;
    }

    // buffer/sampler factories
    WGPUBuffer createBuffer(size_t size, WGPUBufferUsageFlags usage) {
        WGPUBufferDescriptor bufferDesc = {};
        bufferDesc.size = size;
        bufferDesc.usage = usage;
        bufferDesc.mappedAtCreation = false;
        return wgpuDeviceCreateBuffer(device, &bufferDesc);
    }

    WGPUSampler createSampler(WGPUFilterMode filterMode = WGPUFilterMode_Nearest) {
        WGPUSamplerDescriptor samplerDesc = {};
        samplerDesc.nextInChain = nullptr;
        samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
        samplerDesc.magFilter = filterMode;
        samplerDesc.minFilter = filterMode;
        samplerDesc.mipmapFilter = (filterMode == WGPUFilterMode_Linear) ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
        samplerDesc.lodMinClamp = 0.0f;
        samplerDesc.lodMaxClamp = 32.0f;
        samplerDesc.maxAnisotropy = 1;
        return wgpuDeviceCreateSampler(this->device, &samplerDesc);
    }

    // release various resources
    void releaseResource(WGPUBuffer resource) {
        if (resource) wgpuBufferRelease(resource);
    }

    void releaseResource(WGPUTexture resource) {
        if (resource) wgpuTextureRelease(resource);
    }

    void releaseResource(WGPUTextureView resource) {
        if (resource) wgpuTextureViewRelease(resource);
    }

    void releaseResource(WGPUSampler resource) {
        if (resource) wgpuSamplerRelease(resource);
    }

    template<typename T, typename ReleaseFunc>
    void releaseResource(T& resource, ReleaseFunc releaseFunc) {
        if (resource) {
            releaseFunc(resource);
            resource = nullptr;
        }
    }
};

#endif // WGPU_BOILERPLATE_H
