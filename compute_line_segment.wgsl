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
        if (params.segmentPresent[s] == 0) {
            continue;
        }

        let startX = f32(params.segmentStartX[s]);
        let startY = f32(params.segmentStartY[s]);
        let endX = f32(params.segmentEndX[s]);
        let endY = f32(params.segmentEndY[s]);
        let prevStartX = f32(params.segmentPrevStartX[s]);
        let prevStartY = f32(params.segmentPrevStartY[s]);
        let prevEndX = f32(params.segmentPrevEndX[s]);
        let prevEndY = f32(params.segmentPrevEndY[s]);
        let startRadius = params.segmentStartRadius[s];
        let endRadius = params.segmentEndRadius[s];
        let prevStartRadius = params.segmentPrevStartRadius[s];
        let prevEndRadius = params.segmentPrevEndRadius[s];

        let px = f32(i) + 0.5;
        let py = f32(j) + 0.5;

        let wasInPrev = isPointNearSegment(px, py, prevStartX, prevStartY, prevStartRadius, prevEndX, prevEndY, prevEndRadius);
        let isInNew = isPointNearSegment(px, py, startX, startY, startRadius, endX, endY, endRadius);

        if (wasInPrev && !isInNew) {
            // Cell exited segment - reset density
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
