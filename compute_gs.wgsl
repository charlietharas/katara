struct SimParams {
    gridX: i32,
    gridY: i32,
    overrelaxationCoefficient: f32,
};

@group(0) @binding(0) var<uniform> params: SimParams;
@group(0) @binding(1) var divergenceTexture: texture_storage_2d<r32float, read>;
@group(0) @binding(2) var pressureTexture: texture_storage_2d<r32float, read>;
@group(0) @binding(3) var newPressureTexture: texture_storage_2d<r32float, write>;
@group(0) @binding(4) var solidTexture: texture_2d<f32>;

fn gsIteration(id: vec3<u32>, parity: u32) {
    let i = i32(id.x);
    let j = i32(id.y);

    if (i >= params.gridX || j >= params.gridY) { return; }
    if (i == 0 || i == params.gridX - 1 || j == 0 || j == params.gridY - 1) {
        textureStore(newPressureTexture, vec2<i32>(i, j), vec4<f32>(0.0));
        return;
    }

    if (textureLoad(solidTexture, vec2<i32>(i, j), 0).r == 0.0) {
        textureStore(newPressureTexture, vec2<i32>(i, j), vec4<f32>(0.0));
        return;
    }

    let pOld = textureLoad(pressureTexture, vec2<i32>(i, j)).r;

    // checkerboard!
    if ((i + j) % 2 != i32(parity)) {
        textureStore(newPressureTexture, vec2<i32>(i, j), vec4<f32>(pOld));
        return;
    }

    let sx0 = textureLoad(solidTexture, vec2<i32>(i+1, j), 0).r;
    let sx1 = textureLoad(solidTexture, vec2<i32>(i-1, j), 0).r;
    let sy0 = textureLoad(solidTexture, vec2<i32>(i, j+1), 0).r;
    let sy1 = textureLoad(solidTexture, vec2<i32>(i, j-1), 0).r;
    let b = sx0 + sx1 + sy0 + sy1;

    // stability
    let EPSILON = 0.001; // FLAG not in config
    if (b < EPSILON) {
        textureStore(newPressureTexture, vec2<i32>(i, j), vec4<f32>(0.0));
        return;
    }

    let pRight = textureLoad(pressureTexture, vec2<i32>(i+1, j)).r * sx0;
    let pLeft = textureLoad(pressureTexture, vec2<i32>(i-1, j)).r * sx1;
    let pTop = textureLoad(pressureTexture, vec2<i32>(i, j+1)).r * sy0;
    let pBottom = textureLoad(pressureTexture, vec2<i32>(i, j-1)).r * sy1;

    let divergence = textureLoad(divergenceTexture, vec2<i32>(i, j)).r;

    let pGS = (pRight + pLeft + pTop + pBottom - divergence) / b;
    var pNew = pOld + params.overrelaxationCoefficient * (pGS - pOld);

    // clamp to prevent nan propagation
    if (pNew == pNew) {  // nan != nan
        pNew = clamp(pNew, -1000.0, 1000.0);
    } else {
        pNew = 0.0;
    }

    textureStore(newPressureTexture, vec2<i32>(i, j), vec4<f32>(pNew));
}

@compute @workgroup_size(16, 16)
fn gsRedIteration(@builtin(global_invocation_id) id: vec3<u32>) {
    gsIteration(id, 0u);
}

@compute @workgroup_size(16, 16)
fn gsBlackIteration(@builtin(global_invocation_id) id: vec3<u32>) {
    gsIteration(id, 1u);
}
