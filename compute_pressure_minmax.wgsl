struct SimParams {
    gridX: i32,
    gridY: i32,
    pad0: i32,
    pad1: i32,
};

struct MinMaxUniform {
    pressMin : atomic<u32>,
    pressMax : atomic<u32>,
    velMin : atomic<u32>,
    velMax : atomic<u32>,
    densityMin : atomic<u32>,
    densityMax : atomic<u32>,
    densitySumScaled : atomic<u32>,
    fluidCellCount : atomic<u32>,
}

@group(0) @binding(0) var<uniform> params : SimParams;
@group(0) @binding(1) var pressureTexture : texture_storage_2d<r32float, read>;
@group(0) @binding(2) var velocityTexture : texture_storage_2d<rg32float, read>;
@group(0) @binding(3) var solidTexture : texture_storage_2d<r32float, read>;
@group(0) @binding(4) var<storage, read_write> result : MinMaxUniform;
@group(0) @binding(5) var densityTexture : texture_storage_2d<r32float, read>;

fn floatToOrderedUint(value: f32) -> u32 {
    let bits = bitcast<u32>(value);
    let isNegative = (bits & 0x80000000u) != 0u;
    return select(bits ^ 0x80000000u, ~bits, isNegative);
}

@compute @workgroup_size(16, 16)
fn computePressureMinMax(@builtin(global_invocation_id) id : vec3<u32>) {
    let gridX = u32(params.gridX);
    let gridY = u32(params.gridY);
    let i = id.x;
    let j = id.y;

    if (i >= gridX || j >= gridY) {
        return;
    }

    let solid = textureLoad(solidTexture, vec2<i32>(i32(i), i32(j))).r;
    if (solid == 0.0) { // only fluid cells
        return;
    }

    let pressure = textureLoad(pressureTexture, vec2<i32>(i32(i), i32(j))).r;
    let pressBits = floatToOrderedUint(pressure);
    atomicMin(&result.pressMin, pressBits);
    atomicMax(&result.pressMax, pressBits);

    let velocity = textureLoad(velocityTexture, vec2<i32>(i32(i), i32(j)));
    let velMagnitude = sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
    let velBits = floatToOrderedUint(velMagnitude);
    atomicMin(&result.velMin, velBits);
    atomicMax(&result.velMax, velBits);

    let density = textureLoad(densityTexture, vec2<i32>(i32(i), i32(j))).r;
    let densityBits = floatToOrderedUint(density);
    atomicMin(&result.densityMin, densityBits);
    atomicMax(&result.densityMax, densityBits);

    // total smoke/dye amount (scaled) + fluid domain volume (cell count)
    let densityClamped = clamp(density, 0.0, 1.0);
    let densityScaled = u32(densityClamped * 1024.0);
    atomicAdd(&result.densitySumScaled, densityScaled);
    atomicAdd(&result.fluidCellCount, 1u);
}
