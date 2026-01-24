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

@compute @workgroup_size(16, 16)
fn extrapolate(@builtin(global_invocation_id) id: vec3<u32>) {
    let x = i32(id.x);
    let y = i32(id.y);

    if (x >= params.gridX || y >= params.gridY) { return; }

    var velocity = textureLoad(velocityTexture, vec2<i32>(x, y));

    // Top and bottom boundaries (copy x-velocity) - matching CPU implementation
    if (y == 0) {
        // Bottom boundary: copy x-velocity from row 1
        if (y + 1 < params.gridY) {
            let velAbove = textureLoad(velocityTexture, vec2<i32>(x, y + 1));
            velocity.x = velAbove.x;
        }
    } else if (y == params.gridY - 1) {
        // Top boundary: copy x-velocity from row gridY-2
        if (y - 1 >= 0) {
            let velBelow = textureLoad(velocityTexture, vec2<i32>(x, y - 1));
            velocity.x = velBelow.x;
        }
    }

    // Left and right boundaries (copy y-velocity) - matching CPU implementation
    if (x == 0) {
        // Left boundary: copy y-velocity from column 1
        if (x + 1 < params.gridX) {
            let velRight = textureLoad(velocityTexture, vec2<i32>(x + 1, y));
            velocity.y = velRight.y;
        }
    } else if (x == params.gridX - 1) {
        // Right boundary: copy y-velocity from column gridX-2
        if (x - 1 >= 0) {
            let velLeft = textureLoad(velocityTexture, vec2<i32>(x - 1, y));
            velocity.y = velLeft.y;
        }
    }

    textureStore(newVelocityTexture, vec2<i32>(x, y), velocity);
}