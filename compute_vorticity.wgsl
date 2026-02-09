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
@group(0) @binding(3) var solidTexture: texture_storage_2d<r32float, read>;
@group(0) @binding(4) var curlTexture: texture_storage_2d<r32float, read>;

@compute @workgroup_size(16, 16)
fn vorticity(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    let currentVel = textureLoad(velocityTexture, vec2<i32>(i, j));
    textureStore(newVelocityTexture, vec2<i32>(i, j), currentVel);

    if (i <= 1 || i >= params.gridX - 2 || j <= 1 || j >= params.gridY - 2) {
        return;
    }

    if (textureLoad(solidTexture, vec2<i32>(i, j)).x <= 0.0 ||
        textureLoad(solidTexture, vec2<i32>(i-1, j)).x <= 0.0 ||
        textureLoad(solidTexture, vec2<i32>(i+1, j)).x <= 0.0 ||
        textureLoad(solidTexture, vec2<i32>(i, j-1)).x <= 0.0 ||
        textureLoad(solidTexture, vec2<i32>(i, j+1)).x <= 0.0) {
        return;
    }

    let dx = abs(textureLoad(curlTexture, vec2<i32>(i, j-1)).x) - 
             abs(textureLoad(curlTexture, vec2<i32>(i, j+1)).x);
    let dy = abs(textureLoad(curlTexture, vec2<i32>(i+1, j)).x) - 
             abs(textureLoad(curlTexture, vec2<i32>(i-1, j)).x);
    let len = sqrt(dx * dx + dy * dy) + params.vorticityLen;
    let c = textureLoad(curlTexture, vec2<i32>(i, j)).x;

    if (len > 0.0 && c != 0.0) {
        let x = currentVel.x + params.timestep * c * dx * params.vorticity / len;
        let y = currentVel.y + params.timestep * c * dy * params.vorticity / len;

        textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(x, y, 0.0, 0.0));
    }
}