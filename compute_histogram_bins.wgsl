// Histogram Bin Counting Compute Shader
// Counts values into histogram bins using min/max ranges

struct HistogramBins {
    densityBins : array<atomic<i32>, 64>,   // Pressure histogram
    velocityBins : array<atomic<i32>, 64>,   // Velocity magnitude histogram
}

struct MinMaxUniform {
    pressMin : f32,
    pressMax : f32,
    velMin : f32,
    velMax : f32,
};

struct UniformData {
    drawTarget: i32,
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    pressureMin: f32,
    pressureMax: f32,
    drawVelocities: i32,
    velScale: f32,
    windowWidth: f32,
    windowHeight: f32,
    simWidth: f32,
    simHeight: f32,
    disableHistograms: i32,
    densityHistogramMin: f32,
    densityHistogramMax: f32,
    velocityHistogramMin: f32,
    velocityHistogramMax: f32,
    densityHistogramMaxCount: i32,
    velocityHistogramMaxCount: i32,
    densityHistogramBins: array<vec4<i32>, 16>,
    velocityHistogramBins: array<vec4<i32>, 16>,
};

@group(0) @binding(0) var<uniform> uniforms : UniformData;
@group(0) @binding(1) var<uniform> minMaxUniform : MinMaxUniform;
@group(0) @binding(2) var pressureTexture : texture_storage_2d<r32float, read>;
@group(0) @binding(3) var velocityTexture : texture_storage_2d<rg32float, read>;
@group(0) @binding(4) var solidTexture : texture_storage_2d<r32float, read>;
@group(0) @binding(5) var<storage, read_write> bins : HistogramBins;

@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) id : vec3<u32>) {
    let gridX = u32(uniforms.gridX);
    let gridY = u32(uniforms.gridY);
    let i = id.x;
    let j = id.y;

    if (i >= gridX || j >= gridY) {
        return;
    }

    // Read solid value to check if fluid cell
    let solid = textureLoad(solidTexture, vec2<i32>(i32(i), i32(j))).r;
    if (solid < 0.5) {
        return;  // Skip boundary cells
    }

    // Density histogram (pressure)
    let pressure = textureLoad(pressureTexture, vec2<i32>(i32(i), i32(j))).r;
    let pressDelta = minMaxUniform.pressMax - minMaxUniform.pressMin;
    if (pressDelta > 0.0001) {
        let normalized = (pressure - minMaxUniform.pressMin) / pressDelta;
        var bin = i32(normalized * 64.0);
        bin = clamp(bin, 0, 63);
        atomicAdd(&bins.densityBins[u32(bin)], 1);
    }

    // Velocity histogram (magnitude)
    let velocity = textureLoad(velocityTexture, vec2<i32>(i32(i), i32(j)));
    let velMagnitude = sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
    let velDelta = minMaxUniform.velMax - minMaxUniform.velMin;
    if (velDelta > 0.0001) {
        let normalized = (velMagnitude - minMaxUniform.velMin) / velDelta;
        var bin = i32(normalized * 64.0);
        bin = clamp(bin, 0, 63);
        atomicAdd(&bins.velocityBins[u32(bin)], 1);
    }
}
