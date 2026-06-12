fn densityAt(coord: vec2<i32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(densityTexture, clamped, 0).r;
}

fn bloomHeatmap(value: f32, min: f32, max: f32) -> vec3<f32> {
    let t = (value - min) / (max - min);
    let clamped = clamp(t, 0.0, 1.0);

    if (clamped < 0.33) {
        let k = clamped / 0.33;
        return vec3<f32>(0.0, k, 1.0);
    } else if (clamped < 0.66) {
        let k = (clamped - 0.33) / 0.33;
        return vec3<f32>(k, 1.0, 1.0 - k);
    }

    let k = (clamped - 0.66) / 0.34;
    return vec3<f32>(1.0, 1.0 - 0.75 * k, 0.0);
}

fn draw(texCoord: vec2<i32>, pressure: f32, density: f32) -> vec3<f32> {
    let centerDensity = density;
    let threshold = 0.35;
    let softness = 0.10;
    let thresholdMask = smoothstep(threshold, threshold + softness, centerDensity);

    var glowAccum = 0.0;
    var glowSamples = 0;
    for (var oy = -1; oy <= 1; oy++) {
        for (var ox = -1; ox <= 1; ox++) {
            let d = densityAt(texCoord + vec2<i32>(ox, oy));
            glowAccum += max(0.0, d - threshold);
            glowSamples += 1;
        }
    }

    let glow = clamp((glowAccum / f32(glowSamples)) * 2.0, 0.0, 1.0);
    let base = bloomHeatmap(centerDensity, 0.0, 1.0) * (1.0 - 0.35 * thresholdMask);
    let glowColor = vec3<f32>(1.0, 0.75, 0.31) * glow;
    return clamp(base + glowColor, vec3<f32>(0.0), vec3<f32>(1.0));
}
