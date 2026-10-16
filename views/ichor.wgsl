fn densityAt(coord: vec2<i32>, densityTexture: texture_2d<f32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(densityTexture, clamped, 0).r;
}

fn ichorPalette(value: f32, rangeMin: f32, rangeMax: f32) -> vec3<f32> {
    let t = (value - rangeMin) / (rangeMax - rangeMin);
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

#draw
    let solid = textureLoad(solidTexture, texCoord, 0).r;
    if (solid <= 0.5) {
        return vec4<f32>(0.0);
    }

    let centerDensity = densityAt(texCoord, densityTexture);
    let threshold = 0.35;
    let softness = 0.10;
    let thresholdMask = smoothstep(threshold, threshold + softness, centerDensity);

    var glowAccum = 0.0;
    var glowSamples = 0;
    for (var oy = -1; oy <= 1; oy++) {
        for (var ox = -1; ox <= 1; ox++) {
            let d = densityAt(texCoord + vec2<i32>(ox, oy), densityTexture);
            glowAccum += max(0.0, d - threshold);
            glowSamples += 1;
        }
    }

    let glow = clamp((glowAccum / f32(glowSamples)) * 2.0, 0.0, 1.0);
    let base = ichorPalette(centerDensity, 0.0, 1.0) * (1.0 - 0.35 * thresholdMask);
    let glowColor = vec3<f32>(1.0, 0.75, 0.31) * glow;
    let color = clamp(base + glowColor, vec3<f32>(0.0), vec3<f32>(1.0));
    return vec4<f32>(color, 1.0);
