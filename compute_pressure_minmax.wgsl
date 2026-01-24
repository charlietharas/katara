// Pressure + Velocity Min/Max Compute Shader
// Calculates min/max values for pressure field and velocity magnitude

struct MinMaxResult {
    pressMin : atomic<u32>,  // ordered float bits
    pressMax : atomic<u32>,
    velMin : atomic<u32>,    // velocity magnitude min/max
    velMax : atomic<u32>,
}

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
@group(0) @binding(1) var pressureTexture : texture_storage_2d<r32float, read>;
@group(0) @binding(2) var velocityTexture : texture_storage_2d<rg32float, read>;
@group(0) @binding(3) var solidTexture : texture_storage_2d<r32float, read>;
@group(0) @binding(4) var<storage, read_write> result : MinMaxResult;

fn floatToOrderedUint(value: f32) -> u32 {
    let bits = bitcast<u32>(value);
    let isNegative = (bits & 0x80000000u) != 0u;
    return select(bits ^ 0x80000000u, ~bits, isNegative);
}

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

    // Read pressure value
    let pressure = textureLoad(pressureTexture, vec2<i32>(i32(i), i32(j))).r;
    let pressBits = floatToOrderedUint(pressure);
    atomicMin(&result.pressMin, pressBits);
    atomicMax(&result.pressMax, pressBits);

    // Read velocity and compute magnitude
    let velocity = textureLoad(velocityTexture, vec2<i32>(i32(i), i32(j)));
    let velMagnitude = sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
    let velBits = floatToOrderedUint(velMagnitude);
    atomicMin(&result.velMin, velBits);
    atomicMax(&result.velMax, velBits);
}
