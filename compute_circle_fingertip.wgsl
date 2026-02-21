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
    vorticity: f32,
    vorticityLen: f32,
    circleX: array<i32, 42>,
    circleY: array<i32, 42>,
    prevCircleX: array<i32, 42>,
    prevCircleY: array<i32, 42>,
    circleZ: array<f32, 42>,
    circleScaledRadius: array<i32, 42>,
    circlePresent: array<i32, 42>,
    circleWasPresent: array<i32, 42>,
    numCircles: i32,
    baseCircleRadius: i32,
    segmentStartX: array<i32, 46>,
    segmentStartY: array<i32, 46>,
    segmentEndX: array<i32, 46>,
    segmentEndY: array<i32, 46>,
    segmentPrevStartX: array<i32, 46>,
    segmentPrevStartY: array<i32, 46>,
    segmentPrevEndX: array<i32, 46>,
    segmentPrevEndY: array<i32, 46>,
    segmentStartRadius: array<f32, 46>,
    segmentEndRadius: array<f32, 46>,
    segmentPrevStartRadius: array<f32, 46>,
    segmentPrevEndRadius: array<f32, 46>,
    segmentPresent: array<i32, 46>,
    segmentWasPresent: array<i32, 46>,
    numSegments: i32,
    pad0: i32,
    pad1: i32,
    pad2: i32,
};

// new momentum transfer handler for multiple circles
// (in this case corresponding to hand keypoints)
// hence the magic numbers ^
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

    var vel = textureLoad(velocityTexture, vec2<i32>(i, j)).xy;
    let currentSolid = textureLoad(solidTexture, vec2<i32>(i, j)).r;
    var isInAnyCircle = false;
    var wasInAnyPrevCircle = false;

    // process all circles
    for (var c = 0; c < params.numCircles; c = c + 1) {
        if (params.circlePresent[c] == 0) {
            continue;
        }

        let circleX = params.circleX[c];
        let circleY = params.circleY[c];
        let prevCircleX = params.prevCircleX[c];
        let prevCircleY = params.prevCircleY[c];
        let radius = params.circleScaledRadius[c];

        // distance to current and previous circle positions
        let dx = f32(i) + 0.5 - f32(circleX);
        let dy = f32(j) + 0.5 - f32(circleY);
        let distance = sqrt(dx * dx + dy * dy);
        let isInCircle = distance <= f32(radius);

        let prevDx = f32(i) + 0.5 - f32(prevCircleX);
        let prevDy = f32(j) + 0.5 - f32(prevCircleY);
        let prevDist = sqrt(prevDx * prevDx + prevDy * prevDy);
        let wasInPrevCircle = prevDist <= f32(radius);

        // if a cell exited this circle, reset density to 1
        if (wasInPrevCircle && !isInCircle) {
            textureStore(densityTexture, vec2<i32>(i, j), vec4<f32>(1.0, 0.0, 0.0, 0.0));
        }

        if (wasInPrevCircle) {
            vel = vec2<f32>(0.0, 0.0);
            wasInAnyPrevCircle = true;
        }

        if (isInCircle) {
            isInAnyCircle = true;
        }

        // apply momentum to fluid cells near each circle surface
        let deltaX = f32(circleX - prevCircleX);
        let deltaY = f32(circleY - prevCircleY);
        let circleMoved = (deltaX != 0.0) || (deltaY != 0.0);
        let effectiveRadius = f32(radius) + params.momentumTransferRadius;

        if (circleMoved && !isInCircle && distance <= effectiveRadius) {
            // falloff is 1/r^2
            let normalizedDistance = (distance - f32(radius)) / params.momentumTransferRadius;
            var falloff = 1.0 - normalizedDistance * normalizedDistance;
            falloff = max(0.0, falloff);

            let densityFactor = textureLoad(densityTexture, vec2<i32>(i, j)).x;

            let momentumX = deltaX * params.momentumTransferStrength * falloff * densityFactor;
            let momentumY = deltaY * params.momentumTransferStrength * falloff * densityFactor;
            let newVel = vec2<f32>(
                vel.x + momentumX,
                vel.y + momentumY
            );

            // velocity clamping as in legacy
            let maxVel = 8.0;
            vel = vec2<f32>(
                clamp(newVel.x, -maxVel, maxVel),
                clamp(newVel.y, -maxVel, maxVel)
            );
        }
    }

    // if cell is inside any circle, velocity set to 0
    if (isInAnyCircle) {
        vel = vec2<f32>(0.0, 0.0);
    }

    // preserve segment solids when circle runs after line segments
    if (isInAnyCircle) {
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(0.0, 0.0, 0.0, 0.0));
    } else {
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(currentSolid, 0.0, 0.0, 0.0));
    }

    // ping pong
    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(vel, 0.0, 0.0));
}
