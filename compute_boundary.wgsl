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
@group(0) @binding(2) var solidTexture: texture_2d<f32>;
@group(0) @binding(3) var newVelocityTexture: texture_storage_2d<rg32float, write>;
@group(0) @binding(4) var densityTexture: texture_storage_2d<r32float, read>;
@group(0) @binding(5) var newDensityTexture: texture_storage_2d<r32float, write>;

@compute @workgroup_size(16, 16)
fn enforceBoundaryConditions(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    if (i >= params.gridX || j >= params.gridY) { return; }

    let solid = textureLoad(solidTexture, vec2<i32>(i, j), 0).r;
    var velocity = textureLoad(velocityTexture, vec2<i32>(i, j)).xy;
    var density = textureLoad(densityTexture, vec2<i32>(i, j)).r;

    // Clear velocity in solid cells
    if (solid == 0.0f) {
        velocity = vec2<f32>(0.0, 0.0);
        // don't touch density
    }

    // Apply wind tunnel velocity
    if (params.windTunnelSide != -1) {
        if (params.windTunnelSide == 0) { // left
            if (i == 1 && j >= params.windTunnelStart && j < params.windTunnelEnd) {
                velocity.x = params.windTunnelVelocity;
            }
            if (i == 0 && j >= params.windTunnelStart && j < params.windTunnelEnd) {
                velocity.x = params.windTunnelVelocity;
                density = 0.0f;
            }
        } else if (params.windTunnelSide == 1) { // top
            if (j == params.gridY-1 && i >= params.windTunnelStart && i < params.windTunnelEnd) {
                velocity.y = -params.windTunnelVelocity;
                density = 0.0f;
            }
        } else if (params.windTunnelSide == 2) { // bottom
            if (j == 1 && i >= params.windTunnelStart && i < params.windTunnelEnd) {
                velocity.y = params.windTunnelVelocity;
            }
            // Also set velocity at j=2 to ensure flow
            if (j == 2 && i >= params.windTunnelStart && i < params.windTunnelEnd) {
                velocity.y = params.windTunnelVelocity;
            }
            if (j == 0 && i >= params.windTunnelStart && i < params.windTunnelEnd) {
                density = 0.0f;
            }
        } else if (params.windTunnelSide == 3) { // right
            if (i == params.gridX-1 && j >= params.windTunnelStart && j < params.windTunnelEnd) {
                velocity.x = -params.windTunnelVelocity;
                density = 0.0f;
            }
        }
    }

    // Write results
    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(velocity, 0.0, 0.0));
    textureStore(newDensityTexture, vec2<i32>(i, j), vec4<f32>(density));
}