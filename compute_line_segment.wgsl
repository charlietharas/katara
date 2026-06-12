fn packedI32Segment(arr: array<vec4<i32>, 12>, idx: i32) -> i32 {
    let v = arr[u32(idx) / 4u];
    switch idx % 4 {
        case 0: { return v.x; }
        case 1: { return v.y; }
        case 2: { return v.z; }
        default: { return v.w; }
    }
}

fn packedF32Segment(arr: array<vec4<f32>, 12>, idx: i32) -> f32 {
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

@group(0) @binding(0) var<uniform> params: SimParams;
@group(0) @binding(1) var solidTexture: texture_storage_2d<r32float, read_write>;
@group(0) @binding(2) var velocityTexture: texture_storage_2d<rg32float, read>;
@group(0) @binding(3) var newVelocityTexture: texture_storage_2d<rg32float, write>;
@group(0) @binding(4) var densityTexture: texture_storage_2d<r32float, read_write>;

// Check if point is near a line segment with interpolated radii
fn isPointNearSegment(px: f32, py: f32, x1: f32, y1: f32, r1: f32, x2: f32, y2: f32, r2: f32) -> bool {
    let dx = x2 - x1;
    let dy = y2 - y1;
    let length = sqrt(dx * dx + dy * dy);

    if (length < 0.001) {
        // Degenerate case: point
        let dist = sqrt((px - x1) * (px - x1) + (py - y1) * (py - y1));
        return dist <= r1;
    }

    // Normalized direction
    let nx = dx / length;
    let ny = dy / length;

    // Vector from p1 to test point
    let tx = px - x1;
    let ty = py - y1;

    // Project onto line (dot product)
    var t = tx * nx + ty * ny;
    t = clamp(t, 0.0, length);

    // Closest point on segment
    let cx = x1 + t * nx;
    let cy = y1 + t * ny;

    // Interpolate radius at closest point
    let radius = r1 + (r2 - r1) * (t / length);

    // Distance from closest point
    let dist = sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));

    return dist <= radius;
}

@compute @workgroup_size(16, 16)
fn updateLineSegments(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    // boundary
    if (i == 0 || j == 0 || i >= params.gridX - 1 || j >= params.gridY - 1) {
        return;
    }

    var vel = textureLoad(velocityTexture, vec2<i32>(i, j)).xy;
    var isInAnySegment = false;
    var wasInAnyPrevSegment = false;

    // Process all segments
    for (var s = 0; s < params.numSegments; s = s + 1) {
        if (packedI32Segment(params.segmentPresent, s) == 0) {
            continue;
        }

        let startX = f32(packedI32Segment(params.segmentStartX, s));
        let startY = f32(packedI32Segment(params.segmentStartY, s));
        let endX = f32(packedI32Segment(params.segmentEndX, s));
        let endY = f32(packedI32Segment(params.segmentEndY, s));
        let prevStartX = f32(packedI32Segment(params.segmentPrevStartX, s));
        let prevStartY = f32(packedI32Segment(params.segmentPrevStartY, s));
        let prevEndX = f32(packedI32Segment(params.segmentPrevEndX, s));
        let prevEndY = f32(packedI32Segment(params.segmentPrevEndY, s));
        let startRadius = packedF32Segment(params.segmentStartRadius, s);
        let endRadius = packedF32Segment(params.segmentEndRadius, s);
        let prevStartRadius = packedF32Segment(params.segmentPrevStartRadius, s);
        let prevEndRadius = packedF32Segment(params.segmentPrevEndRadius, s);

        let px = f32(i) + 0.5;
        let py = f32(j) + 0.5;

        let wasInPrev = isPointNearSegment(px, py, prevStartX, prevStartY, prevStartRadius, prevEndX, prevEndY, prevEndRadius);
        let isInNew = isPointNearSegment(px, py, startX, startY, startRadius, endX, endY, endRadius);

        if (wasInPrev && !isInNew) {
            textureStore(densityTexture, vec2<i32>(i, j), vec4<f32>(1.0, 0.0, 0.0, 0.0));
        }

        if (wasInPrev) {
            vel = vec2<f32>(0.0, 0.0);
            wasInAnyPrevSegment = true;
        }

        if (isInNew) {
            isInAnySegment = true;
        }
    }

    // if cell is inside any segment, velocity set to 0
    if (isInAnySegment) {
        vel = vec2<f32>(0.0, 0.0);
    }

    // Update solid texture
    if (isInAnySegment) {
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(0.0, 0.0, 0.0, 0.0));
    } else {
        textureStore(solidTexture, vec2<i32>(i, j), vec4<f32>(1.0, 0.0, 0.0, 0.0));
    }

    // ping pong
    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(vel, 0.0, 0.0));
}
