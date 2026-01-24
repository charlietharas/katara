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
    pad0: f32,
    pad1: f32,
    pad2: f32,
};

@group(0) @binding(0) var<uniform> params: SimParams;
@group(0) @binding(1) var velocityTexture: texture_storage_2d<rg32float, read>;
@group(0) @binding(2) var solidTexture: texture_2d<f32>;
@group(0) @binding(3) var newVelocityTexture: texture_storage_2d<rg32float, write>;

fn sampleField(x: f32, y: f32, x_offset: f32, y_offset: f32) -> vec2<f32> {
    // Subtract specific offsets before converting to grid coords
    // This allows us to align the sampling grid to Faces or Centers
    let gx = (x - x_offset) / params.cellSize;
    let gy = (y - y_offset) / params.cellSize;

    let i0 = i32(floor(gx));
    let j0 = i32(floor(gy));
    
    // Clamp to valid texture range
    let i0_c = clamp(i0, 0, params.gridX - 1);
    let j0_c = clamp(j0, 0, params.gridY - 1);
    let i1_c = clamp(i0 + 1, 0, params.gridX - 1);
    let j1_c = clamp(j0 + 1, 0, params.gridY - 1);

    let fx = gx - f32(i0);
    let fy = gy - f32(j0);

    let v00 = textureLoad(velocityTexture, vec2<i32>(i0_c, j0_c)).xy;
    let v10 = textureLoad(velocityTexture, vec2<i32>(i1_c, j0_c)).xy;
    let v01 = textureLoad(velocityTexture, vec2<i32>(i0_c, j1_c)).xy;
    let v11 = textureLoad(velocityTexture, vec2<i32>(i1_c, j1_c)).xy;

    let v0 = mix(v00, v10, fx);
    let v1 = mix(v01, v11, fx);

    return mix(v0, v1, fy);
}

// Neighborhood averaging helpers
fn neighborhoodX(i: i32, j: i32) -> f32 {
    let i_min = max(0, i - 1);
    let i_max = min(params.gridX - 1, i + 1);
    let j_min = max(0, j - 1);
    let j_max = min(params.gridY - 1, j + 1);

    let v00 = textureLoad(velocityTexture, vec2<i32>(i_min, j_min)).x;
    let v10 = textureLoad(velocityTexture, vec2<i32>(i_max, j_min)).x;
    let v01 = textureLoad(velocityTexture, vec2<i32>(i_min, j_max)).x;
    let v11 = textureLoad(velocityTexture, vec2<i32>(i_max, j_max)).x;

    return (v00 + v10 + v01 + v11) * 0.25;
}

fn neighborhoodY(i: i32, j: i32) -> f32 {
    let i_min = max(0, i - 1);
    let i_max = min(params.gridX - 1, i + 1);
    let j_min = max(0, j - 1);
    let j_max = min(params.gridY - 1, j + 1);

    let v00 = textureLoad(velocityTexture, vec2<i32>(i_min, j_min)).y;
    let v10 = textureLoad(velocityTexture, vec2<i32>(i_max, j_min)).y;
    let v01 = textureLoad(velocityTexture, vec2<i32>(i_min, j_max)).y;
    let v11 = textureLoad(velocityTexture, vec2<i32>(i_max, j_max)).y;

    return (v00 + v10 + v01 + v11) * 0.25;
}

@compute @workgroup_size(16, 16)
fn advectVelocity(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = i32(id.x);
    let j = i32(id.y);

    if (i < 1 || i >= params.gridX || j < 1 || j >= params.gridY) {
        textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(0.0));
        return;
    }

    let halfCell = params.cellSize * 0.5;
    
    // Start with current velocity (preserves data if solid)
    var newVel = textureLoad(velocityTexture, vec2<i32>(i, j)).xy;
    let solidCurrent = textureLoad(solidTexture, vec2<i32>(i, j), 0).r;

    if (solidCurrent != 0.0) {
        
        // --- X-VELOCITY (U) ADVECTION ---
        // U lives on vertical faces. Grid is aligned on X, Offset on Y.
        if (i > 0 && j < params.gridY - 1) {
            let solidLeft = textureLoad(solidTexture, vec2<i32>(i-1, j), 0).r;
            if (solidLeft != 0.0) {
                // Position of U-face
                let x0 = f32(i) * params.cellSize; 
                let y0 = f32(j) * params.cellSize + halfCell;

                let u = newVel.x; 
                let v = neighborhoodY(i, j); // Average V at this U-face

                let x_back = x0 - u * params.timeStep;
                let y_back = y0 - v * params.timeStep;

                // CRITICAL FIX: Offset X by 0.0, Y by halfCell
                newVel.x = sampleField(x_back, y_back, 0.0, halfCell).x;
            }
        }

        // --- Y-VELOCITY (V) ADVECTION ---
        // V lives on horizontal faces. Offset on X, Aligned on Y.
        if (j > 0 && i < params.gridX - 1) {
            let solidBelow = textureLoad(solidTexture, vec2<i32>(i, j-1), 0).r;
            if (solidBelow != 0.0) {
                // Position of V-face
                let x0 = f32(i) * params.cellSize + halfCell;
                let y0 = f32(j) * params.cellSize;

                let u = neighborhoodX(i, j); // Average U at this V-face
                let v = newVel.y;

                let x_back = x0 - u * params.timeStep;
                let y_back = y0 - v * params.timeStep;

                // CRITICAL FIX: Offset X by halfCell, Y by 0.0
                newVel.y = sampleField(x_back, y_back, halfCell, 0.0).y;
            }
        }
    }

    textureStore(newVelocityTexture, vec2<i32>(i, j), vec4<f32>(newVel, 0.0, 0.0));
}