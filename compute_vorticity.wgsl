// First shader: Compute curl and store to texture
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
@group(0) @binding(2) var solidTexture: texture_storage_2d<r32float, read>;
@group(0) @binding(3) var curlTexture: texture_storage_2d<r32float, write>;

@compute @workgroup_size(16, 16)
fn computeCurl(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    // Boundary checks - skip edges for curl computation
    if (i <= 0 || i >= params.gridX - 1 || j <= 0 || j >= params.gridY - 1) {
        textureStore(curlTexture, vec2<i32>(i, j), vec4<f32>(0.0, 0.0, 0.0, 0.0));
        return;
    }

    // Check if cell is solid (1.0 = fluid, 0.0 = solid)
    if (textureLoad(solidTexture, vec2<i32>(i, j)).x <= 0.0) {
        textureStore(curlTexture, vec2<i32>(i, j), vec4<f32>(0.0, 0.0, 0.0, 0.0));
        return;
    }

    // Compute curl at center cell
    let vel_ip1 = textureLoad(velocityTexture, vec2<i32>(i, j+1));
    let vel_im1 = textureLoad(velocityTexture, vec2<i32>(i, j-1));
    let vel_jp1 = textureLoad(velocityTexture, vec2<i32>(i-1, j));
    let vel_jm1 = textureLoad(velocityTexture, vec2<i32>(i+1, j));

    let c = vel_ip1.x - vel_im1.x + vel_jp1.y - vel_jm1.y;

    textureStore(curlTexture, vec2<i32>(i, j), vec4<f32>(c, 0.0, 0.0, 0.0));
}