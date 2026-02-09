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
@group(0) @binding(1) var solidTexture: texture_storage_2d<r32float, read_write>;
@group(0) @binding(2) var velocityTexture: texture_storage_2d<rg32float, read>;
@group(0) @binding(3) var newVelocityTexture: texture_storage_2d<rg32float, write>;
@group(0) @binding(4) var densityTexture: texture_storage_2d<r32float, read_write>;

@compute @workgroup_size(16, 16)
fn updateCircle(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    // boundary
    if (i == 0 || j == 0 || i >= params.gridX - 1 || j >= params.gridY - 1) {
        return;
    }

    // distance to current and previous circle positions
    let dx = f32(i) + 0.5 - f32(params.circleX);
    let dy = f32(j) + 0.5 - f32(params.circleY);
    let distance = sqrt(dx * dx + dy * dy);
    let isInCircle = distance <= f32(params.circleRadius);

    let prevDx = f32(i) + 0.5 - f32(params.prevCircleX);
    let prevDy = f32(j) + 0.5 - f32(params.prevCircleY);
    let prevDist = sqrt(prevDx * prevDx + prevDy * prevDy);
    let wasInPrevCircle = prevDist <= f32(params.circleRadius);

    // if a cell exited the circle, reset density to 1 and velocity to 0
    let exitedCircle = wasInPrevCircle && !isInCircle;
    if (wasInPrevCircle) {
        textureStore(densityTexture, vec2<i32>(i, j), vec4<f32>(1.0, 0.0, 0.0, 0.0));
    }

    let rawVel = textureLoad(velocityTexture, vec2<i32>(i, j));
    var vel = rawVel.xy;
    if (wasInPrevCircle) {
        vel = vec2<f32>(0.0, 0.0);
    }

    let densityFactor = textureLoad(densityTexture, vec2<i32>(i, j)).x; // weight velocity imparted by local density

    // apply momentum to fluid cells near the ball surface
    // (only when circle has velocity and was moved this frame)
    let hasVelocity = (abs(params.circleVelX) > 0.001 || abs(params.circleVelY) > 0.001);
    let wasMoved = (params.circleWasMoved != 0);
    let effectiveRadius = f32(params.circleRadius) + params.momentumTransferRadius;

    if (hasVelocity && wasMoved && !isInCircle && distance <= effectiveRadius) {
        // within influence radius but outside ball
        if (distance > f32(params.circleRadius)) {
            // falloff is 1/r^2
            let normalizedDistance = (distance - f32(params.circleRadius)) / params.momentumTransferRadius;
            var falloff = 1.0 - normalizedDistance * normalizedDistance;
            falloff = max(0.0, falloff);

            let momentumX = params.circleVelX * params.momentumTransferStrength * falloff * densityFactor;
            let momentumY = params.circleVelY * params.momentumTransferStrength * falloff * densityFactor;

            let newVel = vec2<f32>(
                vel.x + momentumX,
                vel.y + momentumY
            );
            
            // NOTE: max velocity clamping for force imparted by the circle, for stability
            let maxVel = 8.0; 
            vel = vec2<f32>(
                clamp(newVel.x, -maxVel, maxVel),
                clamp(newVel.y, -maxVel, maxVel)
            );
        }
    }

    // if cell is inside circle, velocity set to 0
    if (isInCircle) {
        vel = vec2<f32>(0.0, 0.0);
    }

    if (isInCircle) {
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(0.0, 0.0, 0.0, 0.0));
    } else {
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(1.0, 0.0, 0.0, 0.0));
    }

    // ping pong
    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(vel, 0.0, 0.0));
}
