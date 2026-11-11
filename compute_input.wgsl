// Merged hand/mouse input pass: skeleton segments + joint circles in one dispatch.
// Circle and segment data live in storage buffers so the shader can index them
// directly (uniform arrays of vec4 are value-copied into every helper call).

// TODO DESLOP

struct SimParams {
    gridX: i32,
    gridY: i32,
    overrelaxationCoefficient: f32,
    timestep: f32,
    cellSize: f32,
    halfCellSize: f32,
    windTunnelSide: i32,
    windTunnelStart: i32,
    windTunnelEnd: i32,
    windTunnelSpeed: f32,
    edges: i32,
    vorticity: f32,
    vorticityLen: f32,
    inputMode: i32,
    numPresentSegments: i32,
    numCircles: i32,
    numSegments: i32,
    momentumTransferStrength: f32,
    momentumTransferRadius: f32,
    momentumTransferDeadZone: f32,
    momentumLowMotionScale: f32,
    momentumLowMotionSoftCeilingMul: f32,
    clearDensityOnVacate: i32,
    _pad2: i32,
};

// Only present-or-just-vacated circles and present segments are uploaded, so
// both loops run over the compacted counts rather than MAX_CIRCLES/MAX_SEGMENTS.
struct CircleGPU {
    pos: vec2<f32>,
    prevPos: vec2<f32>,
    vel: vec2<f32>,
    radius: f32,
    flags: u32,
};

struct SegmentGPU {
    start: vec2<f32>,
    end: vec2<f32>,
    startVel: vec2<f32>,
    endVel: vec2<f32>,
    startRadius: f32,
    endRadius: f32,
    cullRadiusSq: f32, // (halfLength + maxRadius)^2 around the midpoint
    _pad: f32,
};

const INPUT_MODE_MOUSE_PULL: i32 = 1;
const MOVING_WALL_MOMENTUM_SCALE: f32 = 0.25;

const CIRCLE_PRESENT: u32 = 1u;
const CIRCLE_WAS_PRESENT: u32 = 2u;

@group(0) @binding(0) var<uniform> params: SimParams;
@group(0) @binding(1) var solidTexture: texture_2d<f32>;
@group(0) @binding(2) var velocityTexture: texture_storage_2d<rg32float, read>;
@group(0) @binding(3) var newVelocityTexture: texture_storage_2d<rg32float, write>;
@group(0) @binding(4) var densityTexture: texture_2d<f32>;
@group(0) @binding(5) var newSolidTexture: texture_storage_2d<r32float, write>;
@group(0) @binding(6) var newDensityTexture: texture_storage_2d<r32float, write>;
@group(0) @binding(7) var<storage, read> circles: array<CircleGPU>;
@group(0) @binding(8) var<storage, read> segments: array<SegmentGPU>;

// Returns (inside, tFrac) where tFrac is 0..1 along the segment for velocity lerp.
fn segmentCoverage(p: vec2<f32>, s: SegmentGPU) -> vec2<f32> {
    let toMid = p - (s.start + s.end) * 0.5;
    if (dot(toMid, toMid) > s.cullRadiusSq) {
        return vec2<f32>(0.0, 0.0);
    }

    let axis = s.end - s.start;
    let lenSq = dot(axis, axis);
    if (lenSq < 0.000001) {
        let d = p - s.start;
        if (dot(d, d) <= s.startRadius * s.startRadius) {
            return vec2<f32>(1.0, 0.0);
        }
        return vec2<f32>(0.0, 0.0);
    }

    let t = clamp(dot(p - s.start, axis) / lenSq, 0.0, 1.0);
    let radius = s.startRadius + (s.endRadius - s.startRadius) * t;
    let d = p - (s.start + t * axis);
    if (dot(d, d) <= radius * radius) {
        return vec2<f32>(1.0, t);
    }
    return vec2<f32>(0.0, 0.0);
}

fn cellInAnySolid(p: vec2<f32>) -> bool {
    for (var s = 0; s < params.numSegments; s = s + 1) {
        if (segmentCoverage(p, segments[s]).x > 0.5) {
            return true;
        }
    }
    for (var c = 0; c < params.numCircles; c = c + 1) {
        let circle = circles[c];
        let d = p - circle.pos;
        if (dot(d, d) <= circle.radius * circle.radius) {
            return true;
        }
    }
    return false;
}

fn refillDensityFromNeighbors(i: i32, j: i32, fallback: f32) -> f32 {
    var sum = 0.0;
    var count = 0.0;
    let offsets = array<vec2<i32>, 4>(
        vec2<i32>(-1, 0),
        vec2<i32>(1, 0),
        vec2<i32>(0, -1),
        vec2<i32>(0, 1)
    );
    for (var n = 0; n < 4; n = n + 1) {
        let ni = i + offsets[n].x;
        let nj = j + offsets[n].y;
        if (ni <= 0 || nj <= 0 || ni >= params.gridX - 1 || nj >= params.gridY - 1) {
            continue;
        }
        if (cellInAnySolid(vec2<f32>(f32(ni) + 0.5, f32(nj) + 0.5))) {
            continue;
        }
        sum += textureLoad(densityTexture, vec2<i32>(ni, nj), 0).r;
        count += 1.0;
    }
    if (count > 0.0) {
        return sum / count;
    }
    return fallback;
}

@compute @workgroup_size(16, 16)
fn updateInput(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    // Interior-only momentum; velocity was pre-copied so border texels stay valid.
    if (i == 0 || j == 0 || i >= params.gridX - 1 || j >= params.gridY - 1) {
        return;
    }

    let coord = vec2<i32>(i, j);
    let p = vec2<f32>(f32(i) + 0.5, f32(j) + 0.5);
    let mousePull = params.inputMode == INPUT_MODE_MOUSE_PULL;

    var vel = textureLoad(velocityTexture, coord).xy;
    let currentSolid = textureLoad(solidTexture, coord, 0).r;
    let densityFallback = textureLoad(densityTexture, coord, 0).r;

    var isInAnySolid = false;
    var solidVel = vec2<f32>(0.0, 0.0);

    // hand segments
    if (!mousePull) {
        for (var s = 0; s < params.numSegments; s = s + 1) {
            let seg = segments[s];
            let hit = segmentCoverage(p, seg);
            if (hit.x > 0.5) {
                isInAnySolid = true;
                solidVel = mix(seg.startVel, seg.endVel, hit.y);
            }
        }
    }

    // joints / fingertips / mouse cursor
    for (var c = 0; c < params.numCircles; c = c + 1) {
        let circle = circles[c];
        let isPresent = (circle.flags & CIRCLE_PRESENT) != 0u;
        let radiusSq = circle.radius * circle.radius;

        let d = p - circle.pos;
        let distSq = dot(d, d);
        let isInCircle = distSq <= radiusSq;

        let dPrev = p - circle.prevPos;
        let wasInPrevCircle = dot(dPrev, dPrev) <= radiusSq;

        if (isInCircle) {
            isInAnySolid = true;
            if (isPresent) {
                solidVel = circle.vel;
            }
        }

        if (!isPresent) {
            continue;
        }

        var momentum = vec2<f32>(0.0, 0.0);
        var circleMoved = false;

        if (mousePull) {
            // mouse
            let delta = circle.pos - circle.prevPos;
            circleMoved = dot(delta, delta) > 0.0;
            if (circleMoved) {
                momentum = delta * params.momentumTransferStrength;
            }
        } else {
            // hand (smooth velocity, dead zone)
            let velMagSq = dot(circle.vel, circle.vel);
            let deadZone = params.momentumTransferDeadZone;
            circleMoved = velMagSq > 0.0 && (deadZone <= 0.0 || velMagSq >= deadZone * deadZone);
            if (circleMoved) {
                momentum = circle.vel *
                    (params.timestep * params.momentumTransferStrength * MOVING_WALL_MOMENTUM_SCALE);

                var impulseScale = 1.0;
                if (params.momentumLowMotionScale < 1.0 && deadZone > 0.0) {
                    let deadSq = deadZone * deadZone;
                    let softCeilingSq = deadSq * params.momentumLowMotionSoftCeilingMul;
                    if (velMagSq < softCeilingSq) {
                        let t = (velMagSq - deadSq) / (softCeilingSq - deadSq);
                        impulseScale = params.momentumLowMotionScale +
                            (1.0 - params.momentumLowMotionScale) * t;
                    }
                }
                momentum = momentum * impulseScale;
            }
        }

        if (!circleMoved || isInCircle || wasInPrevCircle) {
            continue;
        }

        let effectiveRadius = circle.radius + params.momentumTransferRadius;
        if (distSq > effectiveRadius * effectiveRadius) {
            continue;
        }

        let normalizedDistance = (sqrt(distSq) - circle.radius) / params.momentumTransferRadius;
        let falloff = max(0.0, 1.0 - normalizedDistance * normalizedDistance);
        vel = vel + momentum * falloff * densityFallback;
    }

    if (!mousePull) {
        // vacated (solid last, fluid now)
        if (currentSolid <= 0.5 && !isInAnySolid) {
            var density = 1.0;
            if (params.clearDensityOnVacate == 0) {
                density = refillDensityFromNeighbors(i, j, densityFallback);
            }
            textureStore(newDensityTexture, coord, vec4<f32>(density, 0.0, 0.0, 0.0));
            vel = vec2<f32>(0.0, 0.0);
        }

        if (isInAnySolid) {
            vel = solidVel * params.cellSize;
        }
    }

    if (mousePull) {
        textureStore(newSolidTexture, coord, vec4<f32>(currentSolid, 0.0, 0.0, 0.0));
    } else if (isInAnySolid) {
        textureStore(newSolidTexture, coord, vec4<f32>(0.0, 0.0, 0.0, 0.0));
    } else {
        textureStore(newSolidTexture, coord, vec4<f32>(1.0, 0.0, 0.0, 0.0));
    }

    textureStore(newVelocityTexture, coord, vec4<f32>(vel, 0.0, 0.0));
}

