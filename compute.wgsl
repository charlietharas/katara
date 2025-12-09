struct SimParams {
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    timeStep: f32,
    gravity: f32,
    diffusionRate: f32,
    vorticity: f32,
};

@group(0) @binding(0) var<uniform> params: SimParams;
@group(0) @binding(1) var velocityTexture: texture_storage_2d<rg32float, read>;
@group(0) @binding(2) var newVelocityTexture: texture_storage_2d<rg32float, write>;
@group(0) @binding(3) var solidTexture: texture_2d<f32>;

@compute @workgroup_size(16, 16)
fn integrateMain(@builtin(global_invocation_id) id: vec3<u32>) {
    let x = i32(id.x);
    let y = i32(id.y);

    if (x >= params.gridX || y >= params.gridY) {
        return;
    }

    var velocity = textureLoad(velocityTexture, vec2<i32>(x, y));

    if (params.gravity != 0.0) {
        // ignore boundary cells (replaces loop def'n in CPU version)
        if (x != 0 && x != params.gridX - 1 && y != 0 && y != params.gridY - 1) {
            // check if solid (replicates CPU check)
            let currentSolid = textureLoad(solidTexture, vec2<i32>(x, y), 0).r;
            if (currentSolid != 0.0) {
                let belowSolid = textureLoad(solidTexture, vec2<i32>(x, y - 1), 0).r;
                if (belowSolid != 0.0) {
                    velocity.y = velocity.y + params.gravity * params.timeStep;
                }
            }
        }
    }

    textureStore(newVelocityTexture, vec2<i32>(x, y), velocity);
}