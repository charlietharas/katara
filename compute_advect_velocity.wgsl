struct SimParams {
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    halfCellSize: f32,
    timestep: f32,
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
    let i_next = min(i + 1, params.gridX - 1);
    let j_prev = max(0, j - 1);

    let v00 = textureLoad(velocityTexture, vec2<i32>(i, j_prev)).x;
    let v10 = textureLoad(velocityTexture, vec2<i32>(i_next, j_prev)).x;
    let v01 = textureLoad(velocityTexture, vec2<i32>(i, j)).x;
    let v11 = textureLoad(velocityTexture, vec2<i32>(i_next, j)).x;

    return (v00 + v10 + v01 + v11) * 0.25;
}

fn neighborhoodY(i: i32, j: i32) -> f32 {
    let i_prev = max(0, i - 1);
    let j_next = min(j + 1, params.gridY - 1);

    let v00 = textureLoad(velocityTexture, vec2<i32>(i_prev, j)).y;
    let v10 = textureLoad(velocityTexture, vec2<i32>(i, j)).y;
    let v01 = textureLoad(velocityTexture, vec2<i32>(i_prev, j_next)).y;
    let v11 = textureLoad(velocityTexture, vec2<i32>(i, j_next)).y;

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
        // x vel advection — require fluid neighbors on both sides of this MAC face.
        // Left/bottom walls already gate via solidLeft / solidBelow; right/top walls
        // need the symmetric solidRight check or the first interior column (gridX-2 /
        // gridY-2) gets traced and the inlet jet smears into a diffuse spew.
        if (i > 0 && j < params.gridY - 1) {
            let solidLeft = textureLoad(solidTexture, vec2<i32>(i - 1, j), 0).r;
            if (solidLeft != 0.0) {
                var advectX = true;
                if (i + 1 < params.gridX) {
                    let solidRight = textureLoad(solidTexture, vec2<i32>(i + 1, j), 0).r;
                    advectX = solidRight != 0.0;
                }
                if (advectX) {
                    var x0 = f32(i) * params.cellSize;
                    var y0 = f32(j) * params.cellSize + params.halfCellSize;

                    x0 -= newVel.x * params.timestep;
                    y0 -= neighborhoodY(i, j) * params.timestep;

                    newVel.x = sampleField(x0, y0, 0.0, params.halfCellSize).x;
                }
            }
        }

        // y vel advection — same symmetric high-index wall guard via solidAbove.
        if (j > 0 && i < params.gridX - 1) {
            let solidBelow = textureLoad(solidTexture, vec2<i32>(i, j - 1), 0).r;
            if (solidBelow != 0.0) {
                var advectY = true;
                if (j + 1 < params.gridY) {
                    let solidAbove = textureLoad(solidTexture, vec2<i32>(i, j + 1), 0).r;
                    advectY = solidAbove != 0.0;
                }
                if (advectY) {
                    var x0 = f32(i) * params.cellSize + params.halfCellSize;
                    var y0 = f32(j) * params.cellSize;

                    x0 -= neighborhoodX(i, j) * params.timestep;
                    y0 -= newVel.y * params.timestep;

                    newVel.y = sampleField(x0, y0, params.halfCellSize, 0.0).y;
                }
            }
        }
    }

    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(newVel, 0.0, 0.0));
}