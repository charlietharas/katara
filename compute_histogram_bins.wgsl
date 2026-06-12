struct SimParams {
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    halfCellSize: f32,
}

struct HistogramBins {
    densityBins : array<atomic<i32>, 64>,
    velocityBins : array<atomic<i32>, 64>,
}

struct MinMaxUniform {
    pressMin : f32,
    pressMax : f32,
    velMin : f32,
    velMax : f32,
}

@group(0) @binding(0) var<uniform> params : SimParams;
@group(0) @binding(1) var<uniform> minMaxUniform : MinMaxUniform;
@group(0) @binding(2) var pressureTexture : texture_storage_2d<r32float, read>;
@group(0) @binding(3) var velocityTexture : texture_storage_2d<rg32float, read>;
@group(0) @binding(4) var solidTexture : texture_storage_2d<r32float, read>;
@group(0) @binding(5) var<storage, read_write> bins : HistogramBins;

@compute @workgroup_size(16, 16)
fn computeHistogramBins(@builtin(global_invocation_id) id : vec3<u32>) {
    let gridX = u32(params.gridX);
    let gridY = u32(params.gridY);
    let i = id.x;
    let j = id.y;

    if (i >= gridX || j >= gridY) {
        return;
    }

    let solid = textureLoad(solidTexture, vec2<i32>(i32(i), i32(j))).r;
    if (solid == 0.0) { // only fluid cells
        return;
    }

    // density histogram (using pressure)
    let pressure = textureLoad(pressureTexture, vec2<i32>(i32(i), i32(j))).r;
    let pressDelta = minMaxUniform.pressMax - minMaxUniform.pressMin;
    if (pressDelta > 0.0001) {
        let normalized = (pressure - minMaxUniform.pressMin) / pressDelta;
        var bin = i32(normalized * 64.0);
        bin = clamp(bin, 0, 63);
        atomicAdd(&bins.densityBins[u32(bin)], 1);
    }

    // velocity histogram
    let velocity = textureLoad(velocityTexture, vec2<i32>(i32(i), i32(j)));
    let velMagnitude = length(velocity.xy);
    let velDelta = minMaxUniform.velMax - minMaxUniform.velMin;
    if (velDelta > 0.0001) {
        let normalized = (velMagnitude - minMaxUniform.velMin) / velDelta;
        var bin = i32(normalized * 64.0);
        bin = clamp(bin, 0, 63);
        atomicAdd(&bins.velocityBins[u32(bin)], 1);
    }
}
