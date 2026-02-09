struct SimParams {
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    timestep: f32,
    gravity: f32,
    vorticity: f32,
    vorticityLen: f32,
    projectionIters: f32,
    density: f32,
    windTunnelSide: i32,
    windTunnelStart: i32,
    windTunnelEnd: i32,
    windTunnelSpeed: f32,
    circleX: i32,
    circleY: i32,
    prevCircleX: i32,
    prevCircleY: i32,
    circleRadius: i32,
    circleVelX: f32,
    circleVelY: f32,
    momentumTransferStrength: f32,
    momentumTransferRadius: f32,
    circleWasMoved: i32,
    halfCellSize: f32,
    pad0: f32,
    pad1: f32,
    pad2: f32,
};

struct MinMaxUniform {
    pressMin : atomic<u32>,
    pressMax : atomic<u32>,
    velMin : atomic<u32>,
    velMax : atomic<u32>,
}

@group(0) @binding(0) var<uniform> uniforms : SimParams;
@group(0) @binding(1) var pressureTexture : texture_storage_2d<r32float, read>;
@group(0) @binding(2) var velocityTexture : texture_storage_2d<rg32float, read>;
@group(0) @binding(3) var solidTexture : texture_storage_2d<r32float, read>;
@group(0) @binding(4) var<storage, read_write> result : MinMaxUniform;

fn floatToOrderedUint(value: f32) -> u32 {
    let bits = bitcast<u32>(value);
    let isNegative = (bits & 0x80000000u) != 0u;
    return select(bits ^ 0x80000000u, ~bits, isNegative);
}

@compute @workgroup_size(16, 16)
fn computePressureMinMax(@builtin(global_invocation_id) id : vec3<u32>) {
    let gridX = u32(uniforms.gridX);
    let gridY = u32(uniforms.gridY);
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
}
