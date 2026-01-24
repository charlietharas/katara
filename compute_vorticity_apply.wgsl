// Second shader: Apply vorticity confinement forces using pre-computed curl
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
@group(0) @binding(2) var newVelocityTexture: texture_storage_2d<rg32float, write>;
@group(0) @binding(3) var solidTexture: texture_storage_2d<r32float, read>;
@group(0) @binding(4) var curlTexture: texture_storage_2d<r32float, read>;

@compute @workgroup_size(16, 16)
fn applyVorticityConfinement(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    // Copy velocity by default
    let currentVel = textureLoad(velocityTexture, vec2<i32>(i, j));
    textureStore(newVelocityTexture, vec2<i32>(i, j), currentVel);

    // Boundary checks - skip 2-cell border
    if (i <= 1 || i >= params.gridX - 2 || j <= 1 || j >= params.gridY - 2) {
        return;
    }

    // Check if current cell and neighbors are fluid (1.0 = fluid, 0.0 = solid)
    if (textureLoad(solidTexture, vec2<i32>(i, j)).x <= 0.0 ||
        textureLoad(solidTexture, vec2<i32>(i-1, j)).x <= 0.0 ||
        textureLoad(solidTexture, vec2<i32>(i+1, j)).x <= 0.0 ||
        textureLoad(solidTexture, vec2<i32>(i, j-1)).x <= 0.0 ||
        textureLoad(solidTexture, vec2<i32>(i, j+1)).x <= 0.0) {
        return;
    }

    // Load pre-computed curl values
    let c = textureLoad(curlTexture, vec2<i32>(i, j)).x;
    let curl_i_jm1 = textureLoad(curlTexture, vec2<i32>(i, j-1)).x;  // curl at (i, j-1)
    let curl_i_jp1 = textureLoad(curlTexture, vec2<i32>(i, j+1)).x;  // curl at (i, j+1)
    let curl_ip1_j = textureLoad(curlTexture, vec2<i32>(i+1, j)).x;  // curl at (i+1, j)
    let curl_im1_j = textureLoad(curlTexture, vec2<i32>(i-1, j)).x;  // curl at (i-1, j)

    // Compute gradient of absolute curl (must match CPU: dx uses j±1, dy uses i±1)
    let dx = abs(curl_i_jm1) - abs(curl_i_jp1);
    let dy = abs(curl_ip1_j) - abs(curl_im1_j);

    // Apply confinement force
    let len = sqrt(dx * dx + dy * dy) + params.vorticityLen;
    if (len > 0.0 && c != 0.0) {
        let forceX = params.timeStep * c * dx * params.vorticity / len;
        let forceY = params.timeStep * c * dy * params.vorticity / len;

        let newVel = vec4<f32>(
            currentVel.x + forceX,
            currentVel.y + forceY,
            0.0,
            0.0
        );

        textureStore(newVelocityTexture, vec2<i32>(i, j), newVel);
    }
}