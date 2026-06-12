fn packedI32Circle(arr: array<vec4<i32>, 11>, idx: i32) -> i32 {
    let v = arr[u32(idx) / 4u];
    switch idx % 4 {
        case 0: { return v.x; }
        case 1: { return v.y; }
        case 2: { return v.z; }
        default: { return v.w; }
    }
}

fn packedF32Circle(arr: array<vec4<f32>, 11>, idx: i32) -> f32 {
    let v = arr[u32(idx) / 4u];
    switch idx % 4 {
        case 0: { return v.x; }
        case 1: { return v.y; }
        case 2: { return v.z; }
        default: { return v.w; }
    }
}

struct SimParams {
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    halfCellSize: f32,
    timestep: f32,
    windTunnelSide: i32,
    windTunnelStart: i32,
    windTunnelEnd: i32,
    windTunnelSpeed: f32,
    edges: i32,
    momentumTransferStrength: f32,
    momentumTransferRadius: f32,
    momentumTransferDeadZone: f32,
    vorticity: f32,
    vorticityLen: f32,
    _pad0: i32,
    circleX: array<vec4<i32>, 11>,
    circleY: array<vec4<i32>, 11>,
    prevCircleX: array<vec4<i32>, 11>,
    prevCircleY: array<vec4<i32>, 11>,
    circleZ: array<vec4<f32>, 11>,
    circleScaledRadius: array<vec4<i32>, 11>,
    circlePresent: array<vec4<i32>, 11>,
    circleWasPresent: array<vec4<i32>, 11>,
    circleVelX: array<vec4<f32>, 11>,
    circleVelY: array<vec4<f32>, 11>,
    numCircles: i32,
    baseCircleRadius: i32,
    segmentStartX: array<vec4<i32>, 12>,
    segmentStartY: array<vec4<i32>, 12>,
    segmentEndX: array<vec4<i32>, 12>,
    segmentEndY: array<vec4<i32>, 12>,
    segmentPrevStartX: array<vec4<i32>, 12>,
    segmentPrevStartY: array<vec4<i32>, 12>,
    segmentPrevEndX: array<vec4<i32>, 12>,
    segmentPrevEndY: array<vec4<i32>, 12>,
    segmentStartRadius: array<vec4<f32>, 12>,
    segmentEndRadius: array<vec4<f32>, 12>,
    segmentPrevStartRadius: array<vec4<f32>, 12>,
    segmentPrevEndRadius: array<vec4<f32>, 12>,
    segmentPresent: array<vec4<i32>, 12>,
    segmentWasPresent: array<vec4<i32>, 12>,
    numSegments: i32,
    inputMode: i32,
    numPresentSegments: i32,
    momentumLowMotionScale: f32,
    momentumLowMotionSoftCeilingMul: f32,
    _padEnd: i32,
};

const INPUT_MODE_MOUSE_PULL: i32 = 1;

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
    var shouldClearCircleSolid = false;
    var momentumWasApplied = false;

    // process all circles
    for (var c = 0; c < params.numCircles; c = c + 1) {
        let isPresent = packedI32Circle(params.circlePresent, c) != 0;
        let wasPresent = packedI32Circle(params.circleWasPresent, c) != 0;
        if (!isPresent && !wasPresent) {
            continue;
        }

        let circleX = packedI32Circle(params.circleX, c);
        let circleY = packedI32Circle(params.circleY, c);
        let prevCircleX = packedI32Circle(params.prevCircleX, c);
        let prevCircleY = packedI32Circle(params.prevCircleY, c);
        let radius = packedI32Circle(params.circleScaledRadius, c);

        // distance to current and previous circle positions
        let dx = f32(i) + 0.5 - f32(circleX);
        let dy = f32(j) + 0.5 - f32(circleY);
        let distance = sqrt(dx * dx + dy * dy);
        let isInCircle = distance <= f32(radius);

        let prevDx = f32(i) + 0.5 - f32(prevCircleX);
        let prevDy = f32(j) + 0.5 - f32(prevCircleY);
        let prevDist = sqrt(prevDx * prevDx + prevDy * prevDy);
        let wasInPrevCircle = prevDist <= f32(radius);

        // if a cell exited this circle, reset density and clear the solid footprint
        if (wasInPrevCircle && !isInCircle) {
            textureStore(densityTexture, vec2<i32>(i, j), vec4<f32>(1.0, 0.0, 0.0, 0.0));
            shouldClearCircleSolid = true;
        }

        // track if cell was in any previous circle position (for zeroing at end)
        if (wasInPrevCircle) {
            wasInAnyPrevCircle = true;
        }

        if (isInCircle) {
            isInAnyCircle = true;
        }

        if (!isPresent) {
            continue;
        }

        // apply momentum to fluid cells near each circle surface
        var momentumX = 0.0;
        var momentumY = 0.0;
        var circleMoved = false;

        if (params.inputMode == INPUT_MODE_MOUSE_PULL) {
            // mouse: raw pixel delta, no dead zone — hand-tuned dead zone is in velocity units
            let deltaX = f32(circleX - prevCircleX);
            let deltaY = f32(circleY - prevCircleY);
            let movementSq = deltaX * deltaX + deltaY * deltaY;
            circleMoved = movementSq > 0.0;
            if (circleMoved) {
                momentumX = deltaX * params.momentumTransferStrength;
                momentumY = deltaY * params.momentumTransferStrength;
            }
        } else {
            // hand: smoothed velocity, dead zone filters tracking jitter
            let circleVelX = packedF32Circle(params.circleVelX, c);
            let circleVelY = packedF32Circle(params.circleVelY, c);
            let velMagSq = circleVelX * circleVelX + circleVelY * circleVelY;
            let deadZone = params.momentumTransferDeadZone;
            circleMoved = velMagSq > 0.0 && (deadZone <= 0.0 || velMagSq >= deadZone * deadZone);
            if (circleMoved) {
                momentumX = circleVelX * params.timestep * params.momentumTransferStrength;
                momentumY = circleVelY * params.timestep * params.momentumTransferStrength;

                var impulseScale = 1.0;
                if (params.momentumLowMotionScale < 1.0 && deadZone > 0.0) {
                    let deadSq = deadZone * deadZone;
                    let softCeilingSq = deadSq * params.momentumLowMotionSoftCeilingMul;
                    if (velMagSq < softCeilingSq) {
                        let t = (velMagSq - deadSq) / (softCeilingSq - deadSq);
                        impulseScale = params.momentumLowMotionScale + (1.0 - params.momentumLowMotionScale) * t;
                    }
                }
                momentumX *= impulseScale;
                momentumY *= impulseScale;
            }
        }

        let effectiveRadius = f32(radius) + params.momentumTransferRadius;

        if (circleMoved && !isInCircle && !wasInPrevCircle && distance <= effectiveRadius) {
            // falloff is 1/r^2
            let normalizedDistance = (distance - f32(radius)) / params.momentumTransferRadius;
            var falloff = 1.0 - normalizedDistance * normalizedDistance;
            falloff = max(0.0, falloff);

            let densityFactor = textureLoad(densityTexture, vec2<i32>(i, j)).x;

            let newVel = vec2<f32>(
                vel.x + momentumX * falloff * densityFactor,
                vel.y + momentumY * falloff * densityFactor
            );

            vel = newVel;
            momentumWasApplied = true;
        }
    }

    // zero velocity for cells that left a solid circle footprint, and for
    // cells inside circles, unless they just received nearby momentum
    if (shouldClearCircleSolid) {
        vel = vec2<f32>(0.0, 0.0);
    } else if (params.inputMode != INPUT_MODE_MOUSE_PULL) {
        if (wasInAnyPrevCircle && !momentumWasApplied) {
            vel = vec2<f32>(0.0, 0.0);
        }
        if (isInAnyCircle && !momentumWasApplied) {
            vel = vec2<f32>(0.0, 0.0);
        }
    }

    // mouse pull: momentum only, never write solids
    if (params.inputMode == INPUT_MODE_MOUSE_PULL) {
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(currentSolid, 0.0, 0.0, 0.0));
    } else if (isInAnyCircle) {
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(0.0, 0.0, 0.0, 0.0));
    } else if (params.numPresentSegments == 0) {
        // joints/pointer-tip: circles own the solid mask (same as line segment shader).
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(1.0, 0.0, 0.0, 0.0));
    } else {
        // full mode: line segments already ran and own clearing outside the skeleton.
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(currentSolid, 0.0, 0.0, 0.0));
    }

    // ping pong
    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(vel, 0.0, 0.0));
}
