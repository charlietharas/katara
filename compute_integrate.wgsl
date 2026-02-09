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
@group(0) @binding(2) var newVelocityTexture: texture_storage_2d<rg32float, write>;
@group(0) @binding(3) var solidTexture: texture_2d<f32>;

@compute @workgroup_size(16, 16)
fn integrate(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    if (i >= params.gridX || j >= params.gridY) {
        return;
    }

    var velocity = textureLoad(velocityTexture, vec2<i32>(i, j));

    // ignore boundary cells (replaces loop def'n in CPU version)
    if (i != 0 && i != params.gridX - 1 && j != 0 && j != params.gridY - 1) {
        // check if solid (replicates CPU check)
        let currentSolid = textureLoad(solidTexture, vec2<i32>(i, j), 0).r;
        if (currentSolid != 0.0) {
            let belowSolid = textureLoad(solidTexture, vec2<i32>(i, j - 1), 0).r;
            if (belowSolid != 0.0) {
                velocity.y = velocity.y + params.gravity * params.timestep;
            }
        }
    }

    textureStore(newVelocityTexture, vec2<i32>(i, j), velocity);
}