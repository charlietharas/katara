struct SimParams {
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    halfCellSize: f32,
    timestep: f32,
    density: f32,
    gravity: f32,
    projectionIters: f32,
    windTunnelSide: i32,
    windTunnelStart: i32,
    windTunnelEnd: i32,
    windTunnelSpeed: f32,
    momentumTransferStrength: f32,
    momentumTransferRadius: f32,
    momentumTransferDeadZone: f32,
    vorticity: f32,
    vorticityLen: f32,
    circleX: i32,
    circleY: i32,
    prevCircleX: i32,
    prevCircleY: i32,
    circleRadius: i32,
    pad0: i32,
    pad1: i32,
    pad2: i32,
};

// legacy circle momentum transfer handler
// dynamically injected into web version when we compile with enable_mouse_input

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

    let rawVel = textureLoad(velocityTexture, vec2<i32>(i, j));
    var vel = rawVel.xy;

    // if a cell exited circle, reset density to 1
    if (wasInPrevCircle && !isInCircle) {
        textureStore(densityTexture, vec2<i32>(i, j), vec4<f32>(1.0, 0.0, 0.0, 0.0));
    }

    if (wasInPrevCircle) {
        vel = vec2<f32>(0.0, 0.0);
    }

    // apply momentum to fluid cells near circle surface
    let deltaX = f32(params.circleX - params.prevCircleX);
    let deltaY = f32(params.circleY - params.prevCircleY);
    let movementSq = deltaX * deltaX + deltaY * deltaY;
    let deadZone = params.momentumTransferDeadZone;
    let circleMoved = movementSq > 0.0 && (deadZone <= 0.0 || movementSq >= deadZone * deadZone);
    let effectiveRadius = f32(params.circleRadius) + params.momentumTransferRadius;

    if (circleMoved && !isInCircle && distance <= effectiveRadius) {
        let normalizedDistance = (distance - f32(params.circleRadius)) / params.momentumTransferRadius;
        var falloff = 1.0 - normalizedDistance * normalizedDistance;
        falloff = max(0.0, falloff);

        let densityFactor = textureLoad(densityTexture, vec2<i32>(i, j)).x;
        let momentumX = deltaX * params.momentumTransferStrength * falloff * densityFactor;
        let momentumY = deltaY * params.momentumTransferStrength * falloff * densityFactor;

        let newVel = vec2<f32>(
            vel.x + momentumX,
            vel.y + momentumY
        );

        let maxVel = 8.0;
        vel = vec2<f32>(
            clamp(newVel.x, -maxVel, maxVel),
            clamp(newVel.y, -maxVel, maxVel)
        );
    }

    if (isInCircle) {
        vel = vec2<f32>(0.0, 0.0);
    }

    if (isInCircle) {
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(0.0, 0.0, 0.0, 0.0));
    } else {
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(1.0, 0.0, 0.0, 0.0));
    }

    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(vel, 0.0, 0.0));
}
