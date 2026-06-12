struct UniformData {
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    pressureMin: f32,
    pressureMax: f32,
    velScale: f32,
    windowWidth: f32,
    windowHeight: f32,
    simWidth: f32,
    simHeight: f32,
    disableHistograms: i32,

    // Viewport configuration
    viewportCount: i32,
    viewportX: vec4<i32>,
    viewportY: vec4<i32>,
    viewportWidth: vec4<i32>,
    viewportHeight: vec4<i32>,
    viewportRenderTarget: vec4<i32>,
    viewportRenderVelocity: vec4<i32>,
    viewportRotation: vec4<i32>,

    // Histogram configuration
    densityHistogramEnabled: i32,
    densityHistogramX: i32,
    densityHistogramY: i32,
    densityHistogramWidth: i32,
    densityHistogramHeight: i32,
    velocityHistogramEnabled: i32,
    velocityHistogramX: i32,
    velocityHistogramY: i32,
    velocityHistogramWidth: i32,
    velocityHistogramHeight: i32,
    entropyTimeSeriesEnabled: i32,
    entropyTimeSeriesX: i32,
    entropyTimeSeriesY: i32,
    entropyTimeSeriesWidth: i32,
    entropyTimeSeriesHeight: i32,
    entropyHistoryCount: i32,
    entropyHistoryWriteIndex: i32,
    entropyPad0: i32,
    entropyHistory: array<vec4<f32>, 16>,

    // Volume time series configuration + data (2 lines)
    volumeTimeSeriesEnabled: i32,
    volumeTimeSeriesX: i32,
    volumeTimeSeriesY: i32,
    volumeTimeSeriesWidth: i32,
    volumeTimeSeriesHeight: i32,
    volumeHistoryMax: f32,
    volumeHistoryCount: i32,
    volumeHistoryWriteIndex: i32,
    volumePad0: i32,
    volumeDomainHistory: array<vec4<f32>, 16>,
    volumeMassHistory: array<vec4<f32>, 16>,

    // Histogram data
    densityHistogramMin: f32,
    densityHistogramMax: f32,
    densityHistogramMaxCount: i32,
    velocityHistogramMin: f32,
    velocityHistogramMax: f32,
    velocityHistogramMaxCount: i32,
    pad0: i32,
    densityHistogramBins: array<vec4<i32>, 16>,
    velocityHistogramBins: array<vec4<i32>, 16>,
    backgroundColor: vec4<f32>, // rgb in .xyz, .w unused
};

@group(0) @binding(0) var<uniform> uniforms: UniformData;
@group(0) @binding(1) var pressureSampler: sampler;
@group(0) @binding(2) var pressureTexture: texture_2d<f32>;
@group(0) @binding(3) var densityTexture: texture_2d<f32>;
@group(0) @binding(4) var velocityTexture: texture_2d<f32>;
@group(0) @binding(5) var solidTexture: texture_2d<f32>;
@group(0) @binding(6) var inkTexture: texture_2d<f32>;
