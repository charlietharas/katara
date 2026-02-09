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
@group(0) @binding(2) var solidTexture: texture_2d<f32>;
@group(0) @binding(3) var newVelocityTexture: texture_storage_2d<rg32float, write>;
@group(0) @binding(4) var densityTexture: texture_storage_2d<r32float, read>;
@group(0) @binding(5) var newDensityTexture: texture_storage_2d<r32float, write>;

@compute @workgroup_size(16, 16)
fn enforceBoundaryConditions(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    if (i >= params.gridX || j >= params.gridY) { return; }

    let solid = textureLoad(solidTexture, vec2<i32>(i, j), 0).r;
    var velocity = textureLoad(velocityTexture, vec2<i32>(i, j)).xy;
    var density = textureLoad(densityTexture, vec2<i32>(i, j)).r;

    // idk how to feel ab this
    if (i == 0) {
        velocity.x = 0.0; // left
    }
    if (i == params.gridX - 1) {
        velocity.x = 0.0; // right
    }
    if (j == 0) {
        velocity.y = 0.0; // bottom
    }
    if (j == params.gridY - 1) {
        velocity.y = 0.0; // top
    }

    if (solid == 0.0f) {
        // clear velocity in the solid cell
        velocity = vec2<f32>(0.0, 0.0);
    } else {
        // this is weird and hacky on both the cpu and gpu versions
        // here we clear velocity in all adjacent solid cells
        if (i < params.gridX - 1) {
            let rightSolid = textureLoad(solidTexture, vec2<i32>(i + 1, j), 0).r;
            if (rightSolid == 0.0f) {
                velocity.x = 0.0;
            }
        }

        if (j < params.gridY - 1) {
            let bottomSolid = textureLoad(solidTexture, vec2<i32>(i, j + 1), 0).r;
            if (bottomSolid == 0.0f) {
                velocity.y = 0.0;
            }
        }

        if (i > 0) {
            let leftSolid = textureLoad(solidTexture, vec2<i32>(i - 1, j), 0).r;
            if (leftSolid == 0.0f) {
                velocity.x = 0.0;
            }
        }

        if (j > 0) {
            let topSolid = textureLoad(solidTexture, vec2<i32>(i, j - 1), 0).r;
            if (topSolid == 0.0f) {
                velocity.y = 0.0;
            }
        }
    }

    // preserve wind tunnel velocity
    if (params.windTunnelSide != -1) {
        if (params.windTunnelSide == 0) { // left
            if (i == 1 && j >= params.windTunnelStart && j < params.windTunnelEnd) {
                velocity.x = params.windTunnelSpeed;
            }
            if (i == 0 && j >= params.windTunnelStart && j < params.windTunnelEnd) {
                velocity.x = params.windTunnelSpeed;
                density = 0.0f;
            }
        } else if (params.windTunnelSide == 1) { // top
            if (j == params.gridY-2 && i >= params.windTunnelStart && i < params.windTunnelEnd) {
                velocity.y = -params.windTunnelSpeed;
            }
            if (j == params.gridY-1 && i >= params.windTunnelStart && i < params.windTunnelEnd) {
                velocity.y = -params.windTunnelSpeed;
                density = 0.0f;
            }
        } else if (params.windTunnelSide == 2) { // bottom
            if (j == 1 && i >= params.windTunnelStart && i < params.windTunnelEnd) {
                velocity.y = params.windTunnelSpeed;
            }
            if (j == 0 && i >= params.windTunnelStart && i < params.windTunnelEnd) {
                velocity.y = params.windTunnelSpeed;
                density = 0.0f;
            }
        } else if (params.windTunnelSide == 3) { // right
            if (i == params.gridX-2 && j >= params.windTunnelStart && j < params.windTunnelEnd) {
                velocity.x = -params.windTunnelSpeed;
            }
            if (i == params.gridX-1 && j >= params.windTunnelStart && j < params.windTunnelEnd) {
                velocity.x = -params.windTunnelSpeed;
                density = 0.0f;
            }
        }
    }

    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(velocity, 0.0, 0.0));
    textureStore(newDensityTexture, vec2<i32>(i, j), vec4<f32>(density));
}
