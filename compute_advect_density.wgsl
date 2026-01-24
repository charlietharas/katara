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
@group(0) @binding(2) var densityTexture: texture_storage_2d<r32float, read>;
@group(0) @binding(3) var solidTexture: texture_2d<f32>;
@group(0) @binding(4) var newDensityTexture: texture_storage_2d<r32float, write>;

// Bilinear interpolation for scalar field
fn sampleScalar(x: f32, y: f32, texture: texture_storage_2d<r32float, read>) -> f32 {
    let halfCell = params.cellSize * 0.5;

    let x_clamp = clamp(x, halfCell, (f32(params.gridX) * params.cellSize) - halfCell);
    let y_clamp = clamp(y, halfCell, (f32(params.gridY) * params.cellSize) - halfCell);

    let gx = (x_clamp - halfCell) / params.cellSize;
    let gy = (y_clamp - halfCell) / params.cellSize;

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

    // Copy density for all cells initially (CPU line 320)
    let density = textureLoad(densityTexture, vec2<i32>(i, j)).r;
    var newDensity = density;

    // CPU loops: for (int i = 1; i < gridX; i++) and (int j = 1; j < gridY; j++)
    if (i < 1 || i >= params.gridX || j < 1 || j >= params.gridY) {
        textureStore(newDensityTexture, vec2<i32>(i, j), vec4<f32>(newDensity));
        return;
    }

    let halfCell = params.cellSize * 0.5;

    // CPU version doesn't check solid for density advection!
    // It advects density everywhere, then boundary conditions clear it if needed

    // Sample velocity at cell center (average of faces)
    let vel = textureLoad(velocityTexture, vec2<i32>(i, j));
    let velRight = textureLoad(velocityTexture, vec2<i32>(i+1, j));
    let velTop = textureLoad(velocityTexture, vec2<i32>(i, j+1));

    let velX = (vel.x + velRight.x) * 0.5;
    let velY = (vel.y + velTop.y) * 0.5;

    // Trace back from cell center
    let x0 = f32(i) * params.cellSize + halfCell;
    let y0 = f32(j) * params.cellSize + halfCell;

    let x_back = x0 - velX * params.timeStep;
    let y_back = y0 - velY * params.timeStep;

    // Sample density at traced-back position
    newDensity = sampleScalar(x_back, y_back, densityTexture);

    textureStore(newDensityTexture, vec2<i32>(i, j), vec4<f32>(newDensity));
}