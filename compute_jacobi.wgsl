struct SimParams {
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    timeStep: f32,
    gravity: f32,
    vorticity: f32,
    vorticityLen: f32,
    projectionIters: f32,
    overrelaxationCoeff: f32,
    density: f32,
    windTunnelSide: i32,
    windTunnelStart: i32,
    windTunnelEnd: i32,
    windTunnelVelocity: f32,
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
fn jacobiPressure(@builtin(global_invocation_id) id: vec3<u32>) {
    let x = i32(id.x);
    let y = i32(id.y);

    if (x >= params.gridX || y >= params.gridY) { return; }
    if (x == 0 || x == params.gridX - 1 || y == 0 || y == params.gridY - 1) {
        textureStore(newPressureTexture, vec2<i32>(x, y), vec4<f32>(0.0));
        return;
    }

    let solidCurrent = textureLoad(solidTexture, vec2<i32>(x, y), 0).r;
    if (solidCurrent == 0.0) {
        textureStore(newPressureTexture, vec2<i32>(x, y), vec4<f32>(0.0));
        return;
    }

    let sx0 = textureLoad(solidTexture, vec2<i32>(x + 1, y), 0).r;
    let sx1 = textureLoad(solidTexture, vec2<i32>(x - 1, y), 0).r;
    let sy0 = textureLoad(solidTexture, vec2<i32>(x, y + 1), 0).r;
    let sy1 = textureLoad(solidTexture, vec2<i32>(x, y - 1), 0).r;
    let b = sx0 + sx1 + sy0 + sy1;

    if (b == 0.0) {
        textureStore(newPressureTexture, vec2<i32>(x, y), vec4<f32>(0.0));
        return;
    }

    let pRight = textureLoad(pressureTexture, vec2<i32>(x + 1, y)).r * sx0;
    let pLeft = textureLoad(pressureTexture, vec2<i32>(x - 1, y)).r * sx1;
    let pTop = textureLoad(pressureTexture, vec2<i32>(x, y + 1)).r * sy0;
    let pBottom = textureLoad(pressureTexture, vec2<i32>(x, y - 1)).r * sy1;

    let divergence = textureLoad(divergenceTexture, vec2<i32>(x, y)).r;

    let newPressure = (pRight + pLeft + pTop + pBottom - divergence) / b;

    textureStore(newPressureTexture, vec2<i32>(x, y), vec4<f32>(newPressure));
}