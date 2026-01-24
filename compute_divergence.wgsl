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
@group(0) @binding(1) var velocityTexture: texture_storage_2d<rg32float, read>;
@group(0) @binding(2) var divergenceTexture: texture_storage_2d<r32float, write>;
@group(0) @binding(3) var solidTexture: texture_2d<f32>;

@compute @workgroup_size(16, 16)
fn computeDivergence(@builtin(global_invocation_id) id: vec3<u32>) {
    let x = i32(id.x);
    let y = i32(id.y);

    if (x >= params.gridX || y >= params.gridY) { return; }
    if (x == 0 || x == params.gridX - 1 || y == 0 || y == params.gridY - 1) {
        textureStore(divergenceTexture, vec2<i32>(x, y), vec4<f32>(0.0));
        return;
    }

    let solid = textureLoad(solidTexture, vec2<i32>(x, y), 0).r;
    if (solid == 0.0) {
        textureStore(divergenceTexture, vec2<i32>(x, y), vec4<f32>(0.0));
        return;
    }

    // Use forward stencil to match CPU div(): x[i+1]-x[i] + y[i,j+1]-y[i,j]
    let velRight = textureLoad(velocityTexture, vec2<i32>(x + 1, y));
    let velCenter = textureLoad(velocityTexture, vec2<i32>(x, y));
    let velTop = textureLoad(velocityTexture, vec2<i32>(x, y + 1));
    let velBottom = textureLoad(velocityTexture, vec2<i32>(x, y));

    let divergence = velRight.x - velCenter.x + velTop.y - velBottom.y;
    textureStore(divergenceTexture, vec2<i32>(x, y), vec4<f32>(divergence));
}