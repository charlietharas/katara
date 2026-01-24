// Neighbor velocity clearing compute shader
// This is a separate pass that runs after the main boundary conditions pass
// It clears velocity components in fluid cells that would push fluid into solids
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

@compute @workgroup_size(16, 16)
fn clearNeighborVelocities(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    if (i >= params.gridX || j >= params.gridY) { return; }

    let solid = textureLoad(solidTexture, vec2<i32>(i, j), 0).r;
    var velocity = textureLoad(velocityTexture, vec2<i32>(i, j)).xy;

    // If this is a fluid cell, check if we need to clear velocity components
    // that would push fluid into adjacent solid cells
    if (solid > 0.0f) {  // This is a fluid cell
        // Check if right neighbor is solid - if so, clear x-velocity
        // (x-velocity pushes fluid to the right, into the solid)
        if (i < params.gridX - 1) {
            let rightSolid = textureLoad(solidTexture, vec2<i32>(i + 1, j), 0).r;
            if (rightSolid == 0.0f) {
                velocity.x = 0.0;
            }
        }

        // Check if bottom neighbor is solid - if so, clear y-velocity
        // (y-velocity pushes fluid downward, into the solid)
        if (j < params.gridY - 1) {
            let bottomSolid = textureLoad(solidTexture, vec2<i32>(i, j + 1), 0).r;
            if (bottomSolid == 0.0f) {
                velocity.y = 0.0;
            }
        }
    }

    // Write the updated velocity
    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(velocity, 0.0, 0.0));
}
