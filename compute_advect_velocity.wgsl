struct SimParams {
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    halfCellSize: f32,
    timestep: f32,
    pad0: i32,
    pad1: i32,
    pad2: i32,
};

@group(0) @binding(0) var<uniform> params: SimParams;
@group(0) @binding(1) var velocityTexture: texture_storage_2d<rg32float, read>;
@group(0) @binding(2) var solidTexture: texture_2d<f32>;
@group(0) @binding(3) var newVelocityTexture: texture_storage_2d<rg32float, write>;

fn sampleField(x: f32, y: f32, x_offset: f32, y_offset: f32) -> vec2<f32> {
    let x_clamp = clamp(x, x_offset, (f32(params.gridX) * params.cellSize) - params.halfCellSize + x_offset);
    let y_clamp = clamp(y, y_offset, (f32(params.gridY) * params.cellSize) - params.halfCellSize + y_offset);

    let gx = (x_clamp - x_offset) / params.cellSize;
    let gy = (y_clamp - y_offset) / params.cellSize;

    let i0 = i32(floor(gx));
    let j0 = i32(floor(gy));
    let i1 = min(i0 + 1, params.gridX - 1);
    let j1 = min(j0 + 1, params.gridY - 1);

    let fx = gx - f32(i0);
    let fy = gy - f32(j0);

    let v00 = textureLoad(velocityTexture, vec2<i32>(i0, j0)).xy;
    let v10 = textureLoad(velocityTexture, vec2<i32>(i1, j0)).xy;
    let v01 = textureLoad(velocityTexture, vec2<i32>(i0, j1)).xy;
    let v11 = textureLoad(velocityTexture, vec2<i32>(i1, j1)).xy;

    let v0 = mix(v00, v10, fx);
    let v1 = mix(v01, v11, fx);

    return mix(v0, v1, fy);
}

fn neighborhoodX(i: i32, j: i32) -> f32 {
    let i_min = max(0, i - 1);
    let i_max = min(params.gridX - 1, i + 1);
    let j_min = max(0, j - 1);
    let j_max = min(params.gridY - 1, j + 1);

    let v00 = textureLoad(velocityTexture, vec2<i32>(i_min, j_min)).x;
    let v10 = textureLoad(velocityTexture, vec2<i32>(i_max, j_min)).x;
    let v01 = textureLoad(velocityTexture, vec2<i32>(i_min, j_max)).x;
    let v11 = textureLoad(velocityTexture, vec2<i32>(i_max, j_max)).x;

    return (v00 + v10 + v01 + v11) * 0.25;
}

fn neighborhoodY(i: i32, j: i32) -> f32 {
    let i_min = max(0, i - 1);
    let i_max = min(params.gridX - 1, i + 1);
    let j_min = max(0, j - 1);
    let j_max = min(params.gridY - 1, j + 1);

    let v00 = textureLoad(velocityTexture, vec2<i32>(i_min, j_min)).y;
    let v10 = textureLoad(velocityTexture, vec2<i32>(i_max, j_min)).y;
    let v01 = textureLoad(velocityTexture, vec2<i32>(i_min, j_max)).y;
    let v11 = textureLoad(velocityTexture, vec2<i32>(i_max, j_max)).y;

    return (v00 + v10 + v01 + v11) * 0.25;
}

@compute @workgroup_size(16, 16)
fn advectVelocity(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    if (i < 1 || i >= params.gridX || j < 1 || j >= params.gridY) {
        textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(0.0));
        return;
    }

    var newVel = textureLoad(velocityTexture, vec2<i32>(i, j)).xy;
    let solid = textureLoad(solidTexture, vec2<i32>(i, j), 0).r;

    if (solid != 0.0) {
        // x vel advection
        if (i > 0 && j < params.gridY - 1) {
            let solidLeft = textureLoad(solidTexture, vec2<i32>(i-1, j), 0).r;
            if (solidLeft != 0.0) {
                var x0 = f32(i) * params.cellSize;
                var y0 = f32(j) * params.cellSize + params.halfCellSize;

                x0 -= newVel.x * params.timestep;
                y0 -= neighborhoodY(i, j) * params.timestep;

                newVel.x = sampleField(x0, y0, 0.0, params.halfCellSize).x;
            }
        }

        // y vel advection
        if (j > 0 && i < params.gridX - 1) {
            let solidBelow = textureLoad(solidTexture, vec2<i32>(i, j-1), 0).r;
            if (solidBelow != 0.0) {
                var x0 = f32(i) * params.cellSize + params.halfCellSize;
                var y0 = f32(j) * params.cellSize;

                x0 -= neighborhoodX(i, j) * params.timestep;
                y0 -= newVel.y * params.timestep;

                newVel.y = sampleField(x0, y0, params.halfCellSize, 0.0).y;
            }
        }
    }

    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(newVel, 0.0, 0.0));
}