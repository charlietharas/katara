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
@group(0) @binding(1) var velocityTexture: texture_storage_2d<rg32float, read>;
@group(0) @binding(2) var solidTexture: texture_storage_2d<r32float, read>;
@group(0) @binding(3) var curlTexture: texture_storage_2d<r32float, write>;

@compute @workgroup_size(16, 16)
fn curl(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    // boundary
    if (i <= 0 || i >= params.gridX - 1 || j <= 0 || j >= params.gridY - 1) {
        textureStore(curlTexture, vec2<i32>(i, j), vec4<f32>(0.0, 0.0, 0.0, 0.0));
        return;
    }

    // solid
    if (textureLoad(solidTexture, vec2<i32>(i, j)).x <= 0.0) {
        textureStore(curlTexture, vec2<i32>(i, j), vec4<f32>(0.0, 0.0, 0.0, 0.0));
        return;
    }

    let x_jp1 = textureLoad(velocityTexture, vec2<i32>(i, j+1)).x;
    let x_jm1 = textureLoad(velocityTexture, vec2<i32>(i, j-1)).x;
    let y_im1 = textureLoad(velocityTexture, vec2<i32>(i-1, j)).y;
    let y_ip1 = textureLoad(velocityTexture, vec2<i32>(i+1, j)).y;

    let c = x_jp1 - x_jm1 + y_im1 - y_ip1;

    textureStore(curlTexture, vec2<i32>(i, j), vec4<f32>(c, 0.0, 0.0, 0.0));
}