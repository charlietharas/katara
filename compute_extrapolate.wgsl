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

@compute @workgroup_size(16, 16)
fn extrapolate(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    if (i >= params.gridX || j >= params.gridY) { return; }

    var velocity = textureLoad(velocityTexture, vec2<i32>(i, j));

    // set boundary tiles to copy neighbors
    if (j == 0) {
        if (j + 1 < params.gridY) {
            let velAbove = textureLoad(velocityTexture, vec2<i32>(i, j + 1));
            velocity.x = velAbove.x;
        }
    } else if (j == params.gridY - 1) {
        if (j - 1 >= 0) {
            let velBelow = textureLoad(velocityTexture, vec2<i32>(i, j - 1));
            velocity.x = velBelow.x;
        }
    }
    if (i == 0) {
        if (i + 1 < params.gridX) {
            let velRight = textureLoad(velocityTexture, vec2<i32>(i + 1, j));
            velocity.y = velRight.y;
        }
    } else if (i == params.gridX - 1) {
        if (i - 1 >= 0) {
            let velLeft = textureLoad(velocityTexture, vec2<i32>(i - 1, j));
            velocity.y = velLeft.y;
        }
    }

    textureStore(newVelocityTexture, vec2<i32>(i, j), velocity);
}