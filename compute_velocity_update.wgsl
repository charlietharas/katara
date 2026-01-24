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
@group(0) @binding(2) var newVelocityTexture: texture_storage_2d<rg32float, write>;
@group(0) @binding(3) var solidTexture: texture_2d<f32>;
@group(0) @binding(4) var pressureTexture: texture_storage_2d<r32float, read>;

@compute @workgroup_size(16, 16)
fn updateVelocityFromPressure(@builtin(global_invocation_id) id: vec3<u32>) {
    let x = i32(id.x);
    let y = i32(id.y);

    if (x >= params.gridX || y >= params.gridY) { return; }

    var velocity = textureLoad(velocityTexture, vec2<i32>(x, y));

    // --- X-VELOCITY UPDATE (Left Face of cell x,y) ---
    // Sits between (x-1, y) and (x, y)
    if (x > 0 && x < params.gridX) {
        let solidCurrent = textureLoad(solidTexture, vec2<i32>(x, y), 0).r;
        let solidLeft = textureLoad(solidTexture, vec2<i32>(x-1, y), 0).r; // FIX: Check Left!

        // Only update if BOTH sides of the face are fluid
        if (solidCurrent > 0.0 && solidLeft > 0.0) {
            let pCurrent = textureLoad(pressureTexture, vec2<i32>(x, y)).r;
            let pLeft = textureLoad(pressureTexture, vec2<i32>(x - 1, y)).r;
            
            // Apply gradient (No pressureScale, assuming geometric solve)
            velocity.x -= (pCurrent - pLeft);
        } else {
            // OPTIONAL: Enforce No-Slip/Free-Slip at walls
            // If one side is solid, velocity at the face should be 0.
            velocity.x = 0.0; 
        }
    }

    // --- Y-VELOCITY UPDATE (Bottom Face of cell x,y) ---
    // Sits between (x, y-1) and (x, y)
    if (y > 0 && y < params.gridY) {
        let solidCurrent = textureLoad(solidTexture, vec2<i32>(x, y), 0).r;
        let solidBottom = textureLoad(solidTexture, vec2<i32>(x, y-1), 0).r; // FIX: Check Bottom!

        // Only update if BOTH sides of the face are fluid
        if (solidCurrent > 0.0 && solidBottom > 0.0) {
            let pCurrent = textureLoad(pressureTexture, vec2<i32>(x, y)).r;
            let pBottom = textureLoad(pressureTexture, vec2<i32>(x, y - 1)).r;

            velocity.y -= (pCurrent - pBottom);
        } else {
            velocity.y = 0.0;
        }
    }

    textureStore(newVelocityTexture, vec2<i32>(x, y), velocity);
}