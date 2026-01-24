// Circle boundary update and momentum transfer compute shader
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
    circleX: i32,
    circleY: i32,
    prevCircleX: i32,
    prevCircleY: i32,
    circleRadius: i32,
    circleVelX: f32,
    circleVelY: f32,
    momentumTransferCoeff: f32,
    momentumTransferRadius: f32,
    circleWasMoved: i32,
    pad0: f32,
    pad1: f32,
};

@group(0) @binding(0) var<uniform> params: SimParams;
@group(0) @binding(1) var solidTexture: texture_storage_2d<r32float, read_write>;
@group(0) @binding(2) var velocityTexture: texture_storage_2d<rg32float, read>;
@group(0) @binding(3) var newVelocityTexture: texture_storage_2d<rg32float, write>;
@group(0) @binding(4) var densityTexture: texture_storage_2d<r32float, read_write>;

@compute @workgroup_size(16, 16)
fn updateCircle(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    // Skip cells outside grid
    if (i >= params.gridX || j >= params.gridY) {
        return;
    }

    // Skip boundary cells (edges/walls) - preserve existing boundaries
    if (i == 0 || i == params.gridX - 1 || j == 0 || j == params.gridY - 1) {
        return;
    }

    // Calculate distance to current and previous circle positions
    let dx = f32(i) + 0.5 - f32(params.circleX);
    let dy = f32(j) + 0.5 - f32(params.circleY);
    let dist = sqrt(dx * dx + dy * dy);
    let isInCircle = dist <= f32(params.circleRadius);

    let prevDx = f32(i) + 0.5 - f32(params.prevCircleX);
    let prevDy = f32(j) + 0.5 - f32(params.prevCircleY);
    let prevDist = sqrt(prevDx * prevDx + prevDy * prevDy);
    let wasInPrevCircle = prevDist <= f32(params.circleRadius);

    // Handle cells that exited the circle - reset density to 1.0 and velocity to 0.0
    let exitedCircle = (wasInPrevCircle && !isInCircle);
    if (exitedCircle) {
        textureStore(densityTexture, vec2<i32>(i, j), vec4<f32>(1.0, 0.0, 0.0, 0.0));
    }

    // Read velocity (from previous frame's advection)
    let vel = textureLoad(velocityTexture, vec2<i32>(i, j));
    var resultVel = vel.xy;

    // Clear velocity for cells that exited the circle (matches CPU behavior)
    if (exitedCircle) {
        resultVel = vec2<f32>(0.0, 0.0);
    }

    // Read density (after potential reset for exited cells)
    let density = textureLoad(densityTexture, vec2<i32>(i, j)).x;

    // Handle momentum transfer for cells near the circle (only when circle has velocity and was moved this frame)
    let hasVelocity = (abs(params.circleVelX) > 0.001 || abs(params.circleVelY) > 0.001);
    let wasMoved = (params.circleWasMoved != 0);

    if (hasVelocity && wasMoved && !isInCircle && dist <= f32(params.circleRadius) + params.momentumTransferRadius) {
        if (dist > f32(params.circleRadius)) {
            let normalizedDistance = (dist - f32(params.circleRadius)) / params.momentumTransferRadius;
            var falloff = 1.0 - normalizedDistance * normalizedDistance;
            falloff = max(0.0, falloff);

            let momentumX = params.circleVelX * params.momentumTransferCoeff * falloff * density;
            let momentumY = params.circleVelY * params.momentumTransferCoeff * falloff * density;

            let newVel = vec2<f32>(
                vel.x + momentumX,
                vel.y + momentumY
            );

            let maxVel = 8.0;
            resultVel = vec2<f32>(
                clamp(newVel.x, -maxVel, maxVel),
                clamp(newVel.y, -maxVel, maxVel)
            );
        }
    }

    // Set velocity to zero for cells inside the circle
    if (isInCircle) {
        resultVel = vec2<f32>(0.0, 0.0);
    }

    // Rebuild solid texture from scratch each frame (but skip boundaries)
    if (isInCircle) {
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(0.0, 0.0, 0.0, 0.0));
    } else {
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(1.0, 0.0, 0.0, 0.0));
    }

    // Write final velocity to newVelocityTexture (ping-pong)
    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(resultVel, 0.0, 0.0));
}
