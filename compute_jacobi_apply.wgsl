struct SimParams {
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    halfCellSize: f32,
};

@group(0) @binding(0) var<uniform> params: SimParams;
@group(0) @binding(1) var velocityTexture: texture_storage_2d<rg32float, read>;
@group(0) @binding(2) var newVelocityTexture: texture_storage_2d<rg32float, write>;
@group(0) @binding(3) var solidTexture: texture_2d<f32>;
@group(0) @binding(4) var pressureTexture: texture_storage_2d<r32float, read>;

@compute @workgroup_size(16, 16)
fn applyProjection(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    if (i >= params.gridX || j >= params.gridY) { return; }

    var velocity = textureLoad(velocityTexture, vec2<i32>(i, j));

    // x-velocity sits between cells (i-1, j) and (i, j).
    // Skip outer MAC faces (i=0 and i=gridX-1) so inlet faces pinned by boundary
    // are not zeroed when the wall cell is solid — mirrors CPU Gauss-Seidel bounds.
    if (i > 0 && i < params.gridX - 1) {
        let sx0 = textureLoad(solidTexture, vec2<i32>(i, j), 0).r;
        let sx1 = textureLoad(solidTexture, vec2<i32>(i-1, j), 0).r;

        if (sx0 > 0.0 && sx1 > 0.0) {
            let pCurrent = textureLoad(pressureTexture, vec2<i32>(i, j)).r;
            let pLeft = textureLoad(pressureTexture, vec2<i32>(i-1, j)).r;
            velocity.x -= (pCurrent - pLeft);
        } else {
            velocity.x = 0.0;
        }
    }

    // y-velocity sits between cells (i, j-1) and (i, j).
    if (j > 0 && j < params.gridY - 1) {
        let sy0 = textureLoad(solidTexture, vec2<i32>(i, j), 0).r;
        let sy1 = textureLoad(solidTexture, vec2<i32>(i, j-1), 0).r;

        if (sy0 > 0.0 && sy1 > 0.0) {
            let pCurrent = textureLoad(pressureTexture, vec2<i32>(i, j)).r;
            let pBottom = textureLoad(pressureTexture, vec2<i32>(i, j-1)).r;
            velocity.y -= (pCurrent - pBottom);
        } else {
            velocity.y = 0.0;
        }
    }

    textureStore(newVelocityTexture, vec2<i32>(i, j), velocity);
}