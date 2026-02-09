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


@group(0) @binding(0) var<uniform> params: SimParams;
@group(0) @binding(1) var divergenceTexture: texture_storage_2d<r32float, read>;
@group(0) @binding(2) var pressureTexture: texture_storage_2d<r32float, read>;
@group(0) @binding(3) var newPressureTexture: texture_storage_2d<r32float, write>;
@group(0) @binding(4) var solidTexture: texture_2d<f32>;

@compute @workgroup_size(16, 16)
fn jacobiIteration(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    if (i >= params.gridX || j >= params.gridY) { return; }
    if (i == 0 || i == params.gridX - 1 || j == 0 || j == params.gridY - 1) {
        textureStore(newPressureTexture, vec2<i32>(i, j), vec4<f32>(0.0));
        return;
    }

    if (textureLoad(solidTexture, vec2<i32>(i, j), 0).r == 0.0) {
        textureStore(newPressureTexture, vec2<i32>(i, j), vec4<f32>(0.0));
        return;
    }

    let sx0 = textureLoad(solidTexture, vec2<i32>(i+1, j), 0).r;
    let sx1 = textureLoad(solidTexture, vec2<i32>(i-1, j), 0).r;
    let sy0 = textureLoad(solidTexture, vec2<i32>(i, j+1), 0).r;
    let sy1 = textureLoad(solidTexture, vec2<i32>(i, j-1), 0).r;
    let b = sx0 + sx1 + sy0 + sy1;

    if (b == 0.0) {
        textureStore(newPressureTexture, vec2<i32>(i, j), vec4<f32>(0.0));
        return;
    }

    let pRight = textureLoad(pressureTexture, vec2<i32>(i+1, j)).r * sx0;
    let pLeft = textureLoad(pressureTexture, vec2<i32>(i-1, j)).r * sx1;
    let pTop = textureLoad(pressureTexture, vec2<i32>(i, j+1)).r * sy0;
    let pBottom = textureLoad(pressureTexture, vec2<i32>(i, j-1)).r * sy1;

    let divergence = textureLoad(divergenceTexture, vec2<i32>(i, j)).r;

    let newPressure = (pRight + pLeft + pTop + pBottom - divergence) / b;

    textureStore(newPressureTexture, vec2<i32>(i, j), vec4<f32>(newPressure));
}
