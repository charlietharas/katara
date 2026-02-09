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
@group(0) @binding(2) var densityTexture: texture_storage_2d<r32float, read>;
@group(0) @binding(3) var solidTexture: texture_2d<f32>;
@group(0) @binding(4) var newDensityTexture: texture_storage_2d<r32float, write>;

fn sampleScalar(x: f32, y: f32, texture: texture_storage_2d<r32float, read>) -> f32 {
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

    let v00 = textureLoad(texture, vec2<i32>(i0, j0)).r;
    let v10 = textureLoad(texture, vec2<i32>(i1, j0)).r;
    let v01 = textureLoad(texture, vec2<i32>(i0, j1)).r;
    let v11 = textureLoad(texture, vec2<i32>(i1, j1)).r;

    let v0 = mix(v00, v10, fx);
    let v1 = mix(v01, v11, fx);

    return mix(v0, v1, fy);
}

@compute @workgroup_size(16, 16)
fn advectDensity(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    if (i < 1 || i >= params.gridX || j < 1 || j >= params.gridY) {
        textureStore(newDensityTexture, vec2<i32>(i, j), vec4<f32>(0.0));
        return;
    }

    var newDensity = textureLoad(densityTexture, vec2<i32>(i, j)).r;
    let solid = textureLoad(solidTexture, vec2<i32>(i, j), 0).r;

    if (solid != 0.0) {
        // smoke advection
        let vel = textureLoad(velocityTexture, vec2<i32>(i, j));
        let velRight = textureLoad(velocityTexture, vec2<i32>(i+1, j));
        let velTop = textureLoad(velocityTexture, vec2<i32>(i, j+1));

        let vel_x = (vel.x + velRight.x) * 0.5;
        let vel_y = (vel.y + velTop.y) * 0.5;

        let x0 = f32(i) * params.cellSize + params.halfCellSize;
        let y0 = f32(j) * params.cellSize + params.halfCellSize;

        let x1 = x0 - vel_x * params.timestep;
        let y1 = y0 - vel_y * params.timestep;

        newDensity = sampleScalar(x1, y1, densityTexture);
    }

    textureStore(newDensityTexture, vec2<i32>(i, j), vec4<f32>(newDensity));
}