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
};

const EDGE_LEFT: i32 = 8;
const EDGE_TOP: i32 = 4;
const EDGE_BOTTOM: i32 = 2;
const EDGE_RIGHT: i32 = 1;

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

    let leftWall = (params.edges & EDGE_LEFT) != 0;
    let topWall = (params.edges & EDGE_TOP) != 0;
    let bottomWall = (params.edges & EDGE_BOTTOM) != 0;
    let rightWall = (params.edges & EDGE_RIGHT) != 0;

    if (i == 0) {
        if (leftWall) {
            velocity.x = 0.0;
        } else if (params.gridX > 1) {
            velocity.x = textureLoad(velocityTexture, vec2<i32>(i + 1, j)).x;
        }
    }
    if (i == params.gridX - 1) {
        if (rightWall) {
            velocity.x = 0.0;
        } else if (params.gridX > 1) {
            velocity.x = textureLoad(velocityTexture, vec2<i32>(i - 1, j)).x;
        }
    }
    if (j == 0) {
        if (bottomWall) {
            velocity.y = 0.0;
        } else if (params.gridY > 1) {
            velocity.y = textureLoad(velocityTexture, vec2<i32>(i, j + 1)).y;
        }
    }
    if (j == params.gridY - 1) {
        if (topWall) {
            velocity.y = 0.0;
        } else if (params.gridY > 1) {
            velocity.y = textureLoad(velocityTexture, vec2<i32>(i, j - 1)).y;
        }
    }

    if (solid == 0.0f) {
        velocity = vec2<f32>(0.0, 0.0);
    } else {
        // Zero the velocity samples that lie on a face shared with a solid cell.
        // On this MAC grid velocity.x is the cell's LEFT face and velocity.y its
        // BOTTOM face, so only the LEFT neighbor (i-1) gates velocity.x and the
        // BOTTOM neighbor (j-1) gates velocity.y. The right/top faces belong to
        // cells i+1 / j+1 and are cleared by those invocations.
        if (i > 0) {
            let leftSolid = textureLoad(solidTexture, vec2<i32>(i - 1, j), 0).r;
            if (leftSolid == 0.0f) {
                velocity.x = 0.0;
            }
        }

        if (j > 0) {
            let belowSolid = textureLoad(solidTexture, vec2<i32>(i, j - 1), 0).r;
            if (belowSolid == 0.0f) {
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
                density = 0.0;
            }
        } else if (params.windTunnelSide == 1) { // top
            if (j == params.gridY-1 && i >= params.windTunnelStart && i < params.windTunnelEnd) {
                velocity.y = -params.windTunnelSpeed;
                density = 0.0;
            }
        } else if (params.windTunnelSide == 2) { // bottom
            if (j == 1 && i >= params.windTunnelStart && i < params.windTunnelEnd) {
                velocity.y = params.windTunnelSpeed;
            }
            if (j == 0 && i >= params.windTunnelStart && i < params.windTunnelEnd) {
                velocity.y = params.windTunnelSpeed;
                density = 0.0;
            }
        } else if (params.windTunnelSide == 3) { // right
            if (i == params.gridX-1 && j >= params.windTunnelStart && j < params.windTunnelEnd) {
                velocity.x = -params.windTunnelSpeed;
                density = 0.0;
            }
        }
    }

    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(velocity, 0.0, 0.0));
    textureStore(newDensityTexture, vec2<i32>(i, j), vec4<f32>(density));
}
