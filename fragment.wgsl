struct UniformData {
    drawTarget: i32,
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    pressureMin: f32,
    pressureMax: f32,
    gpuInkMode: i32, // 0=hybrid (R32 at bindings 6-8), 1=gpu (RGBA at binding 6)
    velScale: f32,
    windowWidth: f32,
    windowHeight: f32,
    simWidth: f32,
    simHeight: f32,
    disableHistograms: i32,

    // Viewport configuration
    viewportCount: i32,
    viewportX: vec4<i32>,
    viewportY: vec4<i32>,
    viewportWidth: vec4<i32>,
    viewportHeight: vec4<i32>,
    viewportRenderTarget: vec4<i32>,
    viewportRenderVelocity: vec4<i32>,
    pad1: vec3<i32>,

    // Histogram configuration
    densityHistogramEnabled: i32,
    densityHistogramX: i32,
    densityHistogramY: i32,
    densityHistogramWidth: i32,
    densityHistogramHeight: i32,
    velocityHistogramEnabled: i32,
    velocityHistogramX: i32,
    velocityHistogramY: i32,
    velocityHistogramWidth: i32,
    velocityHistogramHeight: i32,
    entropyTimeSeriesEnabled: i32,
    entropyTimeSeriesX: i32,
    entropyTimeSeriesY: i32,
    entropyTimeSeriesWidth: i32,
    entropyTimeSeriesHeight: i32,
    entropyCurrentValue: f32,
    entropyThreshold: f32,
    entropyBloomStrength: f32,
    entropyAboveThreshold: i32,
    entropyHistoryMax: f32,
    entropyHistoryCount: i32,
    entropyHistoryWriteIndex: i32,
    entropyPad0: i32,
    entropyHistory: array<vec4<f32>, 16>,

    // Volume time series configuration + data (2 lines)
    volumeTimeSeriesEnabled: i32,
    volumeTimeSeriesX: i32,
    volumeTimeSeriesY: i32,
    volumeTimeSeriesWidth: i32,
    volumeTimeSeriesHeight: i32,
    volumeHistoryMax: f32,
    volumeHistoryCount: i32,
    volumeHistoryWriteIndex: i32,
    volumePad0: i32,
    volumeDomainHistory: array<vec4<f32>, 16>,
    volumeMassHistory: array<vec4<f32>, 16>,

    // Histogram data
    densityHistogramMin: f32,
    densityHistogramMax: f32,
    densityHistogramMaxCount: i32,
    velocityHistogramMin: f32,
    velocityHistogramMax: f32,
    velocityHistogramMaxCount: i32,
    pad0: i32,
    densityHistogramBins: array<vec4<i32>, 16>,
    velocityHistogramBins: array<vec4<i32>, 16>,
};

@group(0) @binding(0) var<uniform> uniforms: UniformData;
@group(0) @binding(1) var pressureSampler: sampler;
@group(0) @binding(2) var pressureTexture: texture_2d<f32>;
@group(0) @binding(3) var densityTexture: texture_2d<f32>;
@group(0) @binding(4) var velocityTexture: texture_2d<f32>;
@group(0) @binding(5) var solidTexture: texture_2d<f32>;
@group(0) @binding(6) var inkTexture0: texture_2d<f32>; // R; RGBA in gpu mode
@group(0) @binding(7) var inkTexture1: texture_2d<f32>; // G; unused in gpu mode
@group(0) @binding(8) var inkTexture2: texture_2d<f32>; // B; unused in gpu mode

// TODO MAIN -- WIP ^_^
// we in the browser baby

// TODO bug: up/right wind tunnel behaves differently from down/left
// likely because of the forward texture accesses (e.g. asymmetric +1) somewhere
// TODO bug: fluid spotted (sometimes) leaking out of bottom edge
// TODO bug: maybe something wrong with the volume, entropy calculations
// TODO bug: gravity basically broken

// TODO bug: mouse fidelity bad because of hand smoothing

// TODO big cleanup: test, unify, and document codebase, particularly build steps + config (& generally simplify flow of data/modularize)
//  - fix outdated config stuff (e.g. rendering vs layout)
//  - python script for modifying simParams struct uniformly
// TODO unify cpu and gpu stuff (at least partially)
// TODO rebuild desktop version, test sth basic still works
// TODO writeup html file; interactive architectural diagram; website changes

/*
// TODO LATER custom renderer??

// TODO LATER examine differences in pressure/velocity histograms between GPU/CPU
- see plotting script
// TODO LATER swap to different pressure solver

// TODO LATER integrate gravity with gyroscope on the phone

// TODO WAY LATER lots of tiny memory problems in valgrind, investigate if some of these are my fault
*/

// color helpers
fn mapValueToColor(value: f32, min: f32, max: f32) -> vec3<f32> {
    var clampedValue = clamp(value, min, max - 0.0001);
    var delta = max - min;
    var normalized = select(0.5, (clampedValue - min) / delta, delta != 0.0);

    var m = 0.25;
    var num = i32(normalized / m);
    var s = (normalized - f32(num) * m) / m;

    var color = vec3<f32>(0.0, 0.0, 0.0);

    switch(num) {
        case 0: { color = vec3<f32>(0.0, s, 1.0); break; }
        case 1: { color = vec3<f32>(0.0, 1.0, 1.0 - s); break; }
        case 2: { color = vec3<f32>(s, 1.0, 0.0); break; }
        case 3: { color = vec3<f32>(1.0, 1.0 - s, 0.0); break; }
        default: { color = vec3<f32>(1.0, 0.0, 0.0); break; }
    }

    return color;
}

fn mapValueToGreyscale(value: f32, min: f32, max: f32) -> vec3<f32> {
    var t = (value - min) / (max - min);
    t = clamp(t, 0.0, 1.0);
    return vec3<f32>(t, t, t);
}

fn mapValueToHeatmap(value: f32, min: f32, max: f32) -> vec3<f32> {
    var t = (value - min) / (max - min);
    t = clamp(t, 0.0, 1.0);

    if (t < 0.33) {
        let k = t / 0.33;
        return vec3<f32>(0.0, k, 1.0);
    } else if (t < 0.66) {
        let k = (t - 0.33) / 0.33;
        return vec3<f32>(k, 1.0, 1.0 - k);
    }

    let k = (t - 0.66) / 0.34;
    return vec3<f32>(1.0, 1.0 - 0.75 * k, 0.0);
}

fn getClampedCoord(coord: vec2<i32>) -> vec2<i32> {
    return vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
}

fn sampleDensityClamped(coord: vec2<i32>) -> f32 {
    return textureLoad(densityTexture, getClampedCoord(coord), 0).r;
}

fn sampleVelocityClamped(coord: vec2<i32>) -> vec2<f32> {
    return textureLoad(velocityTexture, getClampedCoord(coord), 0).rg;
}

fn computeDivergenceFromVelocity(coord: vec2<i32>) -> f32 {
    let center = sampleVelocityClamped(coord);
    let right = sampleVelocityClamped(coord + vec2<i32>(1, 0));
    let top = sampleVelocityClamped(coord + vec2<i32>(0, 1));
    let bottom = sampleVelocityClamped(coord + vec2<i32>(0, -1));
    return right.x - center.x + top.y - bottom.y;
}

fn computeLocalDivergenceScale(coord: vec2<i32>) -> f32 {
    var maxAbsDiv = abs(computeDivergenceFromVelocity(coord));
    maxAbsDiv = max(maxAbsDiv, abs(computeDivergenceFromVelocity(coord + vec2<i32>(1, 0))));
    maxAbsDiv = max(maxAbsDiv, abs(computeDivergenceFromVelocity(coord + vec2<i32>(-1, 0))));
    maxAbsDiv = max(maxAbsDiv, abs(computeDivergenceFromVelocity(coord + vec2<i32>(0, 1))));
    maxAbsDiv = max(maxAbsDiv, abs(computeDivergenceFromVelocity(coord + vec2<i32>(0, -1))));
    return max(maxAbsDiv, 1e-4);
}

fn mapDivergenceDebug(divergence: f32, scale: f32) -> vec3<f32> {
    let normalized = clamp(divergence / scale, -1.0, 1.0);
    let magnitude = pow(abs(normalized), 0.65);
    let base = vec3<f32>(0.02, 0.02, 0.025);
    let negColor = vec3<f32>(0.12, 0.38, 1.0);
    let posColor = vec3<f32>(1.0, 0.24, 0.14);
    let signedColor = select(negColor, posColor, normalized >= 0.0);
    return clamp(base + signedColor * magnitude, vec3<f32>(0.0), vec3<f32>(1.0));
}

fn computeNormalLighting(coord: vec2<i32>) -> vec3<f32> {
    let left = sampleDensityClamped(coord + vec2<i32>(-1, 0));
    let right = sampleDensityClamped(coord + vec2<i32>(1, 0));
    let top = sampleDensityClamped(coord + vec2<i32>(0, 1));
    let bottom = sampleDensityClamped(coord + vec2<i32>(0, -1));

    let dx = right - left;
    let dy = top - bottom;
    let normal = normalize(vec3<f32>(-dx * 4.0, -dy * 4.0, 1.0));
    let lightDir = normalize(vec3<f32>(0.45, -0.55, 0.7));
    let diffuse = max(dot(normal, lightDir), 0.0);
    let intensity = 0.2 + 0.8 * diffuse;
    return vec3<f32>(intensity, intensity, intensity);
}

fn renderThresholdBloom(coord: vec2<i32>, centerDensity: f32) -> vec3<f32> {
    let threshold = 0.35;
    let softness = 0.10;
    let thresholdMask = smoothstep(threshold, threshold + softness, centerDensity);

    var glowAccum = 0.0;
    var glowSamples = 0;
    for (var oy = -1; oy <= 1; oy++) {
        for (var ox = -1; ox <= 1; ox++) {
            let d = sampleDensityClamped(coord + vec2<i32>(ox, oy));
            glowAccum += max(0.0, d - threshold);
            glowSamples += 1;
        }
    }

    let glow = clamp((glowAccum / f32(glowSamples)) * 2.0, 0.0, 1.0);
    let base = mapValueToHeatmap(centerDensity, 0.0, 1.0) * (1.0 - 0.35 * thresholdMask);
    let glowColor = vec3<f32>(1.0, 0.75, 0.31) * glow;
    return clamp(base + glowColor, vec3<f32>(0.0), vec3<f32>(1.0));
}

fn getViewportForPixel(pixelCoord: vec2<f32>) -> i32 {
    if (uniforms.viewportCount == 0) {
        return -1; // No viewports defined, use default rendering
    }

    for (var i = 0; i < uniforms.viewportCount; i++) {
        let vx = f32(uniforms.viewportX[i]);
        let vy = f32(uniforms.viewportY[i]);
        let vw = f32(uniforms.viewportWidth[i]);
        let vh = f32(uniforms.viewportHeight[i]);

        if (pixelCoord.x >= vx && pixelCoord.x < vx + vw &&
            pixelCoord.y >= vy && pixelCoord.y < vy + vh) {
            return i;
        }
    }
    return -1; // Not in any viewport
}

fn mapValueToVelocityColor(value: f32, min: f32, max: f32) -> vec3<f32> {
    var clampedValue = clamp(value, min, max - 0.0001);
    var delta = max - min;
    var normalized = select(0.5, (clampedValue - min) / delta, delta != 0.0);

    if (normalized < 0.5) {
        var t = normalized * 2.0;
        return vec3<f32>(1.0, t * 0.647, 0.0); // orange to yellow
    } else {
        var t = (normalized - 0.5) * 2.0;
        return vec3<f32>(1.0, 0.647 + t * 0.353, 0.0); // yellow to white
    }
}

fn mapInkToColor(r: f32, g: f32, b: f32) -> vec3<f32> {
    return vec3<f32>(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0));
}

fn sampleInkColor(coord: vec2<i32>) -> vec3<f32> {
    if (uniforms.gpuInkMode != 0) {
        return textureLoad(inkTexture0, coord, 0).rgb;
    }
    return mapInkToColor(
        textureLoad(inkTexture0, coord, 0).r,
        textureLoad(inkTexture1, coord, 0).r,
        textureLoad(inkTexture2, coord, 0).r
    );
}

fn distanceToLineSegment(point: vec2<f32>, lineStart: vec2<f32>, lineEnd: vec2<f32>) -> f32 {
    var line = lineEnd - lineStart;
    var lineLength = length(line);

    if (lineLength < 0.0001) {
        return distance(point, lineStart);
    }

    var t = max(0.0, min(1.0, dot(point - lineStart, line) / (lineLength * lineLength)));
    var projection = lineStart + t * line;

    return distance(point, projection);
}

// VELOCITY OVERLAY
const VELOCITY_STRIDE: i32 = 12;
// segment length is expressed as a fraction of the stride spacing so glyphs stay
// readable regardless of sim resolution / cells-per-pixel
const VELOCITY_MIN_LEN_FRAC: f32 = 0.4;
const VELOCITY_MAX_LEN_FRAC: f32 = 1.0;
// skip only effectively stationary fluid; no fraction-of-max cutoff
const VELOCITY_MIN_SPEED: f32 = 1e-5;
const VELOCITY_LINE_WIDTH_PX: f32 = 1.0;

fn sampleCenteredVelocity(gridX: i32, gridY: i32) -> vec2<f32> {
    let iRight = min(gridX + 1, uniforms.gridX - 1);
    let jUp = min(gridY + 1, uniforms.gridY - 1);

    let velHere = textureLoad(velocityTexture, vec2<i32>(gridX, gridY), 0).xy;
    let velRight = textureLoad(velocityTexture, vec2<i32>(iRight, gridY), 0).xy;
    let velUp = textureLoad(velocityTexture, vec2<i32>(gridX, jUp), 0).xy;

    let uCenter = 0.5 * (velHere.x + velRight.x);
    let vCenter = 0.5 * (velHere.y + velUp.y);
    return vec2<f32>(uCenter, vCenter);
}

fn drawVelocityField(coord: vec2<f32>, gridX: i32, gridY: i32, simUnitsPerPixel: f32) -> vec4<f32> {
    if (gridX < 0 || gridX >= uniforms.gridX ||
        gridY < 0 || gridY >= uniforms.gridY) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    // snap this pixel to its nearest stride anchor so the whole glyph (which can span
    // many cells) is evaluated by every pixel it covers, not just the anchor cell itself
    let halfStride = VELOCITY_STRIDE / 2;
    let anchorX = clamp(((gridX + halfStride) / VELOCITY_STRIDE) * VELOCITY_STRIDE, 0, uniforms.gridX - 1);
    let anchorY = clamp(((gridY + halfStride) / VELOCITY_STRIDE) * VELOCITY_STRIDE, 0, uniforms.gridY - 1);

    let solid = textureLoad(solidTexture, vec2<i32>(anchorX, anchorY), 0);
    if (solid.r <= 0.5) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    let centeredVel = sampleCenteredVelocity(anchorX, anchorY);
    let speed = length(centeredVel);
    if (speed < VELOCITY_MIN_SPEED) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    let speedMin = uniforms.velocityHistogramMin;
    let speedMax = max(uniforms.velocityHistogramMax, speedMin + 0.0001);
    let speedNorm = clamp((speed - speedMin) / (speedMax - speedMin), 0.0, 1.0);
    let strideWorld = f32(VELOCITY_STRIDE) * uniforms.cellSize;
    let lengthFrac = mix(VELOCITY_MIN_LEN_FRAC, VELOCITY_MAX_LEN_FRAC, pow(speedNorm, 0.6));
    let segmentLengthWorld = lengthFrac * strideWorld;

    let dir = centeredVel / speed;
    let center = vec2<f32>(
        (f32(anchorX) + 0.5) * uniforms.cellSize,
        (f32(anchorY) + 0.5) * uniforms.cellSize
    );
    // center the glyph on the cell; brightness ramps tail -> tip to show direction
    let lineStart = center - dir * segmentLengthWorld * 0.5;
    let lineEnd = center + dir * segmentLengthWorld * 0.5;

    let lineWidthWorld = VELOCITY_LINE_WIDTH_PX * simUnitsPerPixel;
    let edgeWidthWorld = max(simUnitsPerPixel, uniforms.cellSize * 0.05);
    let line = lineEnd - lineStart;
    let lineLenSq = max(dot(line, line), 0.00000001);
    let t = clamp(dot(coord - lineStart, line) / lineLenSq, 0.0, 1.0);
    let projected = lineStart + t * line;
    let dist = distance(coord, projected);

    // teardrop profile: width tapers toward the tail so each glyph reads as a brush
    // stroke / comet rather than a blunt bar; the tip keeps a full rounded cap
    let widthProfile = mix(0.12, 1.0, t);
    let halfWidth = lineWidthWorld * widthProfile;
    let strokeAlpha = 1.0 - smoothstep(halfWidth, halfWidth + edgeWidthWorld, dist);

    // strength via velocity histogram palette; tail -> tip luminance keeps direction readable
    let velColor = mapValueToVelocityColor(speed, speedMin, speedMax);
    let headBrightness = mix(0.45, 1.0, smoothstep(0.0, 1.0, t));
    let alpha = strokeAlpha * headBrightness * 0.85;
    if (alpha > 0.0) {
        return vec4<f32>(velColor * headBrightness, alpha);
    }

    return vec4<f32>(0.0, 0.0, 0.0, 0.0);
}

fn getBinCount(vec: vec4<i32>, component: i32) -> i32 {
    if (component == 0) { return vec.x; }
    if (component == 1) { return vec.y; }
    if (component == 2) { return vec.z; }
    return vec.w;
}

fn entropyHistorySample(sampleIndex: i32) -> f32 {
    let clampedIndex = clamp(sampleIndex, 0, 63);
    let vecIndex = clampedIndex / 4;
    let component = clampedIndex % 4;
    let packed = uniforms.entropyHistory[vecIndex];
    if (component == 0) { return packed.x; }
    if (component == 1) { return packed.y; }
    if (component == 2) { return packed.z; }
    return packed.w;
}

fn volumeDomainSample(sampleIndex: i32) -> f32 {
    let clampedIndex = clamp(sampleIndex, 0, 63);
    let vecIndex = clampedIndex / 4;
    let component = clampedIndex % 4;
    let packed = uniforms.volumeDomainHistory[vecIndex];
    if (component == 0) { return packed.x; }
    if (component == 1) { return packed.y; }
    if (component == 2) { return packed.z; }
    return packed.w;
}

fn volumeMassSample(sampleIndex: i32) -> f32 {
    let clampedIndex = clamp(sampleIndex, 0, 63);
    let vecIndex = clampedIndex / 4;
    let component = clampedIndex % 4;
    let packed = uniforms.volumeMassHistory[vecIndex];
    if (component == 0) { return packed.x; }
    if (component == 1) { return packed.y; }
    if (component == 2) { return packed.z; }
    return packed.w;
}

fn drawEntropyIndicator(pixelCoord: vec2<f32>) -> vec4<f32> {
    if (uniforms.entropyTimeSeriesEnabled == 0) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    let panelX = f32(uniforms.entropyTimeSeriesX);
    let panelY = f32(uniforms.entropyTimeSeriesY);
    let panelWidth = f32(uniforms.entropyTimeSeriesWidth);
    let panelHeight = f32(uniforms.entropyTimeSeriesHeight);
    if (!(pixelCoord.x >= panelX && pixelCoord.x < panelX + panelWidth &&
          pixelCoord.y >= panelY && pixelCoord.y < panelY + panelHeight)) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    let localX = pixelCoord.x - panelX;
    let localY = pixelCoord.y - panelY;
    let bg = vec3<f32>(28.0 / 255.0, 28.0 / 255.0, 32.0 / 255.0);
    let border = vec3<f32>(200.0 / 255.0, 200.0 / 255.0, 210.0 / 255.0);
    var result = bg;

    if (localX < 1.0 || localX >= panelWidth - 1.0 || localY < 1.0 || localY >= panelHeight - 1.0) {
        return vec4<f32>(border, 1.0);
    }

    let innerX = localX - 1.0;
    let innerY = localY - 1.0;
    let innerWidth = panelWidth - 2.0;
    let innerHeight = panelHeight - 2.0;
    if (innerWidth <= 1.0 || innerHeight <= 1.0 ||
        innerX < 0.0 || innerX >= innerWidth || innerY < 0.0 || innerY >= innerHeight) {
        return vec4<f32>(result, 1.0);
    }

    let sampleCount = clamp(uniforms.entropyHistoryCount, 0, 64);
    if (sampleCount <= 1) {
        return vec4<f32>(result, 1.0);
    }

    let oldestIndex = select(0, uniforms.entropyHistoryWriteIndex, sampleCount == 64);
    let normalizedX = clamp(innerX / max(1.0, innerWidth - 1.0), 0.0, 1.0);
    let samplePos = normalizedX * f32(sampleCount - 1);
    let baseIndex = i32(floor(samplePos));

    var entropyMinDist = 1e9;
    for (var offset = -1; offset <= 1; offset++) {
        let segStart = clamp(baseIndex + offset, 0, sampleCount - 2);
        let segEnd = segStart + 1;

        let ringStart = (oldestIndex + segStart) % 64;
        let ringEnd = (oldestIndex + segEnd) % 64;

        let yStart = (1.0 - clamp(entropyHistorySample(ringStart), 0.0, 1.0)) * innerHeight;
        let yEnd = (1.0 - clamp(entropyHistorySample(ringEnd), 0.0, 1.0)) * innerHeight;
        let xStart = (f32(segStart) / f32(sampleCount - 1)) * (innerWidth - 1.0);
        let xEnd = (f32(segEnd) / f32(sampleCount - 1)) * (innerWidth - 1.0);

        let dist = distanceToLineSegment(
            vec2<f32>(innerX, innerY),
            vec2<f32>(xStart, yStart),
            vec2<f32>(xEnd, yEnd)
        );
        entropyMinDist = min(entropyMinDist, dist);
    }

    if (entropyMinDist <= 2.1) {
        result = vec3<f32>(0.20, 0.72, 0.98);
    }

    let latestIndex = (uniforms.entropyHistoryWriteIndex + 63) % 64;
    let latestEntropy = entropyHistorySample(latestIndex);
    let latestY = (1.0 - clamp(latestEntropy, 0.0, 1.0)) * innerHeight;
    let pointDist = distance(vec2<f32>(innerX, innerY), vec2<f32>(innerWidth - 1.0, latestY));
    if (pointDist <= 2.0) {
        result = vec3<f32>(0.92, 0.96, 1.0);
    }

    return vec4<f32>(result, 1.0);
}

fn drawVolumeTimeSeries(pixelCoord: vec2<f32>) -> vec4<f32> {
    if (uniforms.volumeTimeSeriesEnabled == 0) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    let panelX = f32(uniforms.volumeTimeSeriesX);
    let panelY = f32(uniforms.volumeTimeSeriesY);
    let panelWidth = f32(uniforms.volumeTimeSeriesWidth);
    let panelHeight = f32(uniforms.volumeTimeSeriesHeight);
    if (!(pixelCoord.x >= panelX && pixelCoord.x < panelX + panelWidth &&
          pixelCoord.y >= panelY && pixelCoord.y < panelY + panelHeight)) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    let localX = pixelCoord.x - panelX;
    let localY = pixelCoord.y - panelY;
    let bg = vec3<f32>(28.0 / 255.0, 28.0 / 255.0, 32.0 / 255.0);
    let border = vec3<f32>(200.0 / 255.0, 200.0 / 255.0, 210.0 / 255.0);
    var result = bg;

    if (localX < 1.0 || localX >= panelWidth - 1.0 || localY < 1.0 || localY >= panelHeight - 1.0) {
        return vec4<f32>(border, 1.0);
    }

    let innerX = localX - 1.0;
    let innerY = localY - 1.0;
    let innerWidth = panelWidth - 2.0;
    let innerHeight = panelHeight - 2.0;
    if (innerWidth <= 1.0 || innerHeight <= 1.0 ||
        innerX < 0.0 || innerX >= innerWidth || innerY < 0.0 || innerY >= innerHeight) {
        return vec4<f32>(result, 1.0);
    }

    let sampleCount = clamp(uniforms.volumeHistoryCount, 0, 64);
    if (sampleCount <= 1) {
        return vec4<f32>(result, 1.0);
    }

    let maxVal = max(1e-6, uniforms.volumeHistoryMax);
    let oldestIndex = select(0, uniforms.volumeHistoryWriteIndex, sampleCount == 64);

    let normalizedX = clamp(innerX / max(1.0, innerWidth - 1.0), 0.0, 1.0);
    let samplePos = normalizedX * f32(sampleCount - 1);
    let baseIndex = i32(floor(samplePos));

    var domMinDist = 1e9;
    var massMinDist = 1e9;
    for (var offset = -1; offset <= 1; offset++) {
        let segStart = clamp(baseIndex + offset, 0, sampleCount - 2);
        let segEnd = segStart + 1;

        let ringStart = (oldestIndex + segStart) % 64;
        let ringEnd = (oldestIndex + segEnd) % 64;

        let xStart = (f32(segStart) / f32(sampleCount - 1)) * (innerWidth - 1.0);
        let xEnd = (f32(segEnd) / f32(sampleCount - 1)) * (innerWidth - 1.0);

        let domYStart = (1.0 - clamp(volumeDomainSample(ringStart) / maxVal, 0.0, 1.0)) * innerHeight;
        let domYEnd = (1.0 - clamp(volumeDomainSample(ringEnd) / maxVal, 0.0, 1.0)) * innerHeight;
        let domDist = distanceToLineSegment(
            vec2<f32>(innerX, innerY),
            vec2<f32>(xStart, domYStart),
            vec2<f32>(xEnd, domYEnd)
        );
        domMinDist = min(domMinDist, domDist);

        let massYStart = (1.0 - clamp(volumeMassSample(ringStart) / maxVal, 0.0, 1.0)) * innerHeight;
        let massYEnd = (1.0 - clamp(volumeMassSample(ringEnd) / maxVal, 0.0, 1.0)) * innerHeight;
        let massDist = distanceToLineSegment(
            vec2<f32>(innerX, innerY),
            vec2<f32>(xStart, massYStart),
            vec2<f32>(xEnd, massYEnd)
        );
        massMinDist = min(massMinDist, massDist);
    }

    if (domMinDist <= 2.0) {
        result = vec3<f32>(0.70, 0.72, 0.78); // domain volume (muted)
    }
    if (massMinDist <= 2.0) {
        result = vec3<f32>(0.16, 0.92, 0.55); // smoke mass (bright)
    }

    let latestIndex = (uniforms.volumeHistoryWriteIndex + 63) % 64;
    let latestDom = volumeDomainSample(latestIndex);
    let latestMass = volumeMassSample(latestIndex);
    let latestDomY = (1.0 - clamp(latestDom / maxVal, 0.0, 1.0)) * innerHeight;
    let latestMassY = (1.0 - clamp(latestMass / maxVal, 0.0, 1.0)) * innerHeight;

    let domPointDist = distance(vec2<f32>(innerX, innerY), vec2<f32>(innerWidth - 1.0, latestDomY));
    if (domPointDist <= 2.0) {
        result = vec3<f32>(0.92, 0.94, 0.98);
    }
    let massPointDist = distance(vec2<f32>(innerX, innerY), vec2<f32>(innerWidth - 1.0, latestMassY));
    if (massPointDist <= 2.0) {
        result = vec3<f32>(0.92, 0.98, 0.92);
    }

    return vec4<f32>(result, 1.0);
}

fn drawHistograms(pixelCoord: vec2<f32>) -> vec4<f32> {
    if (uniforms.disableHistograms != 0) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    let volumeIndicator = drawVolumeTimeSeries(pixelCoord);
    if (volumeIndicator.a > 0.0) {
        return volumeIndicator;
    }

    let entropyIndicator = drawEntropyIndicator(pixelCoord);
    if (entropyIndicator.a > 0.0) {
        return entropyIndicator;
    }

    // Draw density histogram (configurable position)
    if (uniforms.densityHistogramEnabled != 0) {
        let dhistX = f32(uniforms.densityHistogramX);
        let dhistY = f32(uniforms.densityHistogramY);
        let histWidth = f32(uniforms.densityHistogramWidth);
        let histHeight = f32(uniforms.densityHistogramHeight);

        if (pixelCoord.x >= dhistX && pixelCoord.x < dhistX + histWidth &&
            pixelCoord.y >= dhistY && pixelCoord.y < dhistY + histHeight) {

            let localX = pixelCoord.x - dhistX;
            let localY = pixelCoord.y - dhistY;

            // background
            let bg = 40.0 / 255.0;
            var result = vec3<f32>(bg, bg, bg);

            // border
            let border = 200.0 / 255.0;
            if (localX < 1.0 || localX >= histWidth - 1.0 || localY < 1.0 || localY >= histHeight - 1.0) {
                result = vec3<f32>(border, border, border);
            } else {
                // bar area
                let barAreaX = localX - 10.0;
                let barAreaY = localY - 10.0;
                let barAreaWidth = histWidth - 20.0;
                let barAreaHeight = histHeight - 20.0;

                if (barAreaX >= 0.0 && barAreaX < barAreaWidth && barAreaY >= 0.0 && barAreaY < barAreaHeight) {
                    let barWidth = histWidth / 64.0;
                    var binIndex = i32(barAreaX / barWidth);
                    binIndex = clamp(binIndex, 0, 63);

                    let maxCount = uniforms.densityHistogramMaxCount;

                    if (maxCount > 0) {
                        let vecIndex = binIndex / 4;
                        let component = binIndex % 4;
                        let vec = uniforms.densityHistogramBins[vecIndex];
                        let binCount = getBinCount(vec, component);
                        let barHeight = (f32(binCount) / f32(maxCount)) * barAreaHeight;
                        let barBottom = barAreaHeight - barHeight;

                        // within bar
                        if (barAreaY >= barBottom && barAreaY < barAreaHeight) {
                            let normalized = f32(binIndex) / 64.0;
                            result = mapValueToColor(normalized, 0.0, 1.0);
                        }
                    }
                }
            }

            return vec4<f32>(result, 1.0);
        }
    }

    // Draw velocity histogram (configurable position)
    if (uniforms.velocityHistogramEnabled != 0) {
        let vhistX = f32(uniforms.velocityHistogramX);
        let vhistY = f32(uniforms.velocityHistogramY);
        let histWidth = f32(uniforms.velocityHistogramWidth);
        let histHeight = f32(uniforms.velocityHistogramHeight);

        if (pixelCoord.x >= vhistX && pixelCoord.x < vhistX + histWidth &&
            pixelCoord.y >= vhistY && pixelCoord.y < vhistY + histHeight) {

            let localX = pixelCoord.x - vhistX;
            let localY = pixelCoord.y - vhistY;

            // background
            let bg = 40.0 / 255.0;
            var result = vec3<f32>(bg, bg, bg);

            // border
            let border = 200.0 / 255.0;
            if (localX < 1.0 || localX >= histWidth - 1.0 || localY < 1.0 || localY >= histHeight - 1.0) {
                result = vec3<f32>(border, border, border);
            } else {
                // bar area
                let barAreaX = localX - 10.0;
                let barAreaY = localY - 10.0;
                let barAreaWidth = histWidth - 20.0;
                let barAreaHeight = histHeight - 20.0;

                if (barAreaX >= 0.0 && barAreaX < barAreaWidth && barAreaY >= 0.0 && barAreaY < barAreaHeight) {
                    let barWidth = histWidth / 64.0;
                    var binIndex = i32(barAreaX / barWidth);
                    binIndex = clamp(binIndex, 0, 63);

                    let maxCount = uniforms.velocityHistogramMaxCount;

                    if (maxCount > 0) {
                        let vecIndex = binIndex / 4;
                        let component = binIndex % 4;
                        let vec = uniforms.velocityHistogramBins[vecIndex];
                        let binCount = getBinCount(vec, component);
                        let barHeight = (f32(binCount) / f32(maxCount)) * barAreaHeight;
                        let barBottom = barAreaHeight - barHeight;

                        // within bar
                        if (barAreaY >= barBottom && barAreaY < barAreaHeight) {
                            let normalized = f32(binIndex) / 64.0;
                            result = mapValueToVelocityColor(normalized, 0.0, 1.0);
                        }
                    }
                }
            }

            return vec4<f32>(result, 1.0);
        }
    }

    return vec4<f32>(0.0, 0.0, 0.0, 0.0);
}

@fragment
fn fs_main(@builtin(position) fragCoord: vec4<f32>) -> @location(0) vec4<f32> {
    let pixelCoord = fragCoord.xy;

    // histograms first (on top of everything)
    let histColor = drawHistograms(pixelCoord);
    if (histColor.a > 0.0) {
        return histColor;
    }

    // Check which viewport we're in
    let viewportIndex = getViewportForPixel(pixelCoord);

    var finalColor: vec3<f32> = vec3<f32>(0.0);

    if (viewportIndex >= 0) {
        // Render to viewport
        let vx = f32(uniforms.viewportX[viewportIndex]);
        let vy = f32(uniforms.viewportY[viewportIndex]);
        let vw = f32(uniforms.viewportWidth[viewportIndex]);
        let vh = f32(uniforms.viewportHeight[viewportIndex]);

        // Transform pixel to simulation coordinates relative to viewport
        let normX = (pixelCoord.x - vx) / vw;
        let normY = (pixelCoord.y - vy) / vh;
        let worldCoord = vec2<f32>(
            normX * uniforms.simWidth,
            (1.0 - normY) * uniforms.simHeight  // flip Y for WebGL convention
        );

        // Convert to grid coordinates
        let simCoord = worldCoord / uniforms.cellSize;
        let texCoord = vec2<i32>(i32(simCoord.x), i32(simCoord.y));

        // Guard against sampling outside the simulation domain
        if (texCoord.x < 0 || texCoord.x >= uniforms.gridX ||
            texCoord.y < 0 || texCoord.y >= uniforms.gridY) {
            return vec4<f32>(0.0, 0.0, 0.0, 1.0);
        }

        // Use viewport's render target
        let viewportRenderTarget = uniforms.viewportRenderTarget[viewportIndex];

        // Load and render simulation data
        let pressure = textureLoad(pressureTexture, texCoord, 0).r;
        let density = textureLoad(densityTexture, texCoord, 0).r;
        let solid = textureLoad(solidTexture, texCoord, 0).r;

        if (solid > 0.5) {
            if (viewportRenderTarget == 0) {
                finalColor = mapValueToColor(pressure, uniforms.pressureMin, uniforms.pressureMax);
            } else if (viewportRenderTarget == 1) {
                finalColor = mapValueToGreyscale(density, 0.0, 1.0);
            } else if (viewportRenderTarget == 2) {
                finalColor = mapValueToColor(pressure, uniforms.pressureMin, uniforms.pressureMax);
                finalColor = finalColor - density * vec3<f32>(1.0, 1.0, 1.0);
                finalColor = max(finalColor, vec3<f32>(0.0, 0.0, 0.0));
            } else if (viewportRenderTarget == 3) {
                finalColor = sampleInkColor(texCoord);
            } else if (viewportRenderTarget == 4) {
                let divergence = computeDivergenceFromVelocity(texCoord);
                let divergenceScale = computeLocalDivergenceScale(texCoord);
                finalColor = mapDivergenceDebug(divergence, divergenceScale);
            } else if (viewportRenderTarget == 5) {
                finalColor = mapValueToHeatmap(density, 0.0, 1.0);
            } else if (viewportRenderTarget == 6) {
                finalColor = computeNormalLighting(texCoord);
            } else if (viewportRenderTarget == 7) {
                finalColor = renderThresholdBloom(texCoord, density);
            }
        } else {
            finalColor = vec3<f32>(0.47);
        }

        // Draw velocity field if enabled for this viewport
        if (uniforms.viewportRenderVelocity[viewportIndex] != 0) {
            let simUnitsPerPixel = max(uniforms.simWidth / vw, uniforms.simHeight / vh);
            var velColor = drawVelocityField(worldCoord, texCoord.x, texCoord.y, simUnitsPerPixel);
            if (velColor.a > 0.0) {
                finalColor = velColor.rgb * velColor.a + finalColor * (1.0 - velColor.a);
            }
        }
    } else {
        // background (matches charliemax.dev --offblack)
        finalColor = vec3<f32>(5.0 / 255.0);
    }

    return vec4<f32>(finalColor, 1.0);
}