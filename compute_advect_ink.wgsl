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
@group(0) @binding(2) var inkTexture: texture_storage_2d<rgba32float, read>;
@group(0) @binding(3) var solidTexture: texture_2d<f32>;
@group(0) @binding(4) var newInkTexture: texture_storage_2d<rgba32float, write>;

fn sampleInk(x: f32, y: f32, texture: texture_storage_2d<rgba32float, read>) -> vec4<f32> {
    let x_clamp = clamp(x, params.halfCellSize, (f32(params.gridX) * params.cellSize) - params.halfCellSize);
    let y_clamp = clamp(y, params.halfCellSize, (f32(params.gridY) * params.cellSize) - params.halfCellSize);

    let gx = (x_clamp - params.halfCellSize) / params.cellSize;
    let gy = (y_clamp - params.halfCellSize) / params.cellSize;

    let i0 = i32(floor(gx));
    let j0 = i32(floor(gy));
    let i1 = min(i0 + 1, params.gridX - 1);
    let j1 = min(j0 + 1, params.gridY - 1);

    let fx = gx - f32(i0);
    let fy = gy - f32(j0);

    let v00 = textureLoad(texture, vec2<i32>(i0, j0));
    let v10 = textureLoad(texture, vec2<i32>(i1, j0));
    let v01 = textureLoad(texture, vec2<i32>(i0, j1));
    let v11 = textureLoad(texture, vec2<i32>(i1, j1));

    let v0 = mix(v00, v10, fx);
    let v1 = mix(v01, v11, fx);

    return mix(v0, v1, fy);
}

@compute @workgroup_size(16, 16)
fn advectInk(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    if (i < 1 || i >= params.gridX || j < 1 || j >= params.gridY) {
        textureStore(newInkTexture, vec2<i32>(i, j), vec4<f32>(0.0));
        return;
    }

    var newInk = textureLoad(inkTexture, vec2<i32>(i, j));
    let solid = textureLoad(solidTexture, vec2<i32>(i, j), 0).r;

    if (solid != 0.0) {
        // ink advection
        let vel = textureLoad(velocityTexture, vec2<i32>(i, j));
        let velRight = textureLoad(velocityTexture, vec2<i32>(i+1, j));
        let velTop = textureLoad(velocityTexture, vec2<i32>(i, j+1));

        let vel_x = (vel.x + velRight.x) * 0.5;
        let vel_y = (vel.y + velTop.y) * 0.5;

        let x0 = f32(i) * params.cellSize + params.halfCellSize;
        let y0 = f32(j) * params.cellSize + params.halfCellSize;

        let x1 = x0 - vel_x * params.timestep;
        let y1 = y0 - vel_y * params.timestep;

        newInk = sampleInk(x1, y1, inkTexture);
    }

    textureStore(newInkTexture, vec2<i32>(i, j), newInk);
}