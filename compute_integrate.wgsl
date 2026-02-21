struct SimParams {
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    halfCellSize: f32,
    timestep: f32,
    density: f32,
    gravity: f32,
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

    if (i != 0 && i != params.gridX - 1 && j != 0 && j != params.gridY - 1) {
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