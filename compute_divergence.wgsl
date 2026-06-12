struct SimParams {
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    halfCellSize: f32,
};

@group(0) @binding(0) var<uniform> params: SimParams;
@group(0) @binding(1) var velocityTexture: texture_storage_2d<rg32float, read>;
@group(0) @binding(2) var divergenceTexture: texture_storage_2d<r32float, write>;
@group(0) @binding(3) var solidTexture: texture_2d<f32>;

@compute @workgroup_size(16, 16)
fn divergence(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    // bounds
    if (i >= params.gridX || j >= params.gridY) { return; }
    // boundary
    if (i == 0 || i == params.gridX - 1 || j == 0 || j == params.gridY - 1) {
        textureStore(divergenceTexture, vec2<i32>(i, j), vec4<f32>(0.0));
        return;
    }

    // solid
    let solid = textureLoad(solidTexture, vec2<i32>(i, j), 0).r;
    if (solid == 0.0) {
        textureStore(divergenceTexture, vec2<i32>(i, j), vec4<f32>(0.0));
        return;
    }

    // divergence (forward stencil)
    let velRight = textureLoad(velocityTexture, vec2<i32>(i + 1, j));
    let velCenter = textureLoad(velocityTexture, vec2<i32>(i, j));
    let velTop = textureLoad(velocityTexture, vec2<i32>(i, j + 1));
    let velBottom = textureLoad(velocityTexture, vec2<i32>(i, j));

    let divergence = velRight.x - velCenter.x + velTop.y - velBottom.y;
    textureStore(divergenceTexture, vec2<i32>(i, j), vec4<f32>(divergence));
}