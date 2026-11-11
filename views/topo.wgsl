fn densityAt(coord: vec2<i32>, densityTexture: texture_2d<f32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(densityTexture, clamped, 0).r;
}

fn topoPalette(t: f32) -> vec3<f32> {
    let cyan = vec3<f32>(0.15, 0.95, 0.95);
    let violet = vec3<f32>(0.45, 0.35, 1.0);
    let magenta = vec3<f32>(1.0, 0.25, 0.75);
    let amber = vec3<f32>(1.0, 0.72, 0.25);

    let x = fract(t);
    var color = mix(cyan, violet, smoothstep(0.0, 0.33, x));
    color = mix(color, magenta, smoothstep(0.33, 0.66, x));
    color = mix(color, amber, smoothstep(0.66, 1.0, x));
    return color;
}

#draw
    let solid = textureLoad(solidTexture, texCoord, 0).r;
    if (solid <= 0.5) {
        return vec4<f32>(0.0);
    }

    let density = densityAt(texCoord, densityTexture);
    let left = densityAt(texCoord + vec2<i32>(-1, 0), densityTexture);
    let right = densityAt(texCoord + vec2<i32>(1, 0), densityTexture);
    let top = densityAt(texCoord + vec2<i32>(0, 1), densityTexture);
    let bottom = densityAt(texCoord + vec2<i32>(0, -1), densityTexture);
    let grad = vec2<f32>(right - left, top - bottom);

    let velocity = textureLoad(velocityTexture, texCoord, 0).xy;
    let speedMin = uniforms.velocityHistogramMin;
    let speedMax = max(uniforms.velocityHistogramMax, speedMin + 0.0001);
    let speedNorm = clamp(
        (length(velocity) - speedMin) / (speedMax - speedMin),
        0.0,
        1.0
    );

    const BANDS: f32 = 14.0;
    let bandPos = density * BANDS;
    let bandIndex = floor(bandPos);
    let frac = fract(bandPos);
    let distToLine = min(frac, 1.0 - frac);

    let gradMag = max(length(grad) * BANDS, 0.02);
    let lineWidth = 0.045;
    let aa = gradMag * 1.2;
    let line = 1.0 - smoothstep(lineWidth, lineWidth + aa, distToLine);

    let isIndex = (i32(bandIndex) % 4) == 0;
    let indexBoost = select(1.0, 1.35, isIndex);

    let normal = normalize(vec3<f32>(-grad * 6.0, 1.0));
    let sunDir = normalize(vec3<f32>(0.45, -0.6, 0.7));
    let relief = max(dot(normal, sunDir), 0.0);

    var fill = vec3<f32>(0.012, 0.016, 0.03);
    fill = fill * (0.6 + 0.8 * relief)
        * (0.35 + 0.65 * clamp(density * 2.0, 0.0, 1.0));

    let hueT = bandIndex * 0.05 + speedNorm * 0.35 + density * 0.1;
    var lineColor = topoPalette(hueT);

    let presence = smoothstep(0.005, 0.06, density);
    let lineGlow = line * presence * indexBoost
        * (0.45 + 0.75 * pow(speedNorm, 0.5));

    var color = fill + lineColor * lineGlow;

    let bloom = (
        1.0 - smoothstep(lineWidth * 3.0, lineWidth * 3.0 + aa * 3.0, distToLine)
    ) * presence;
    color = color + lineColor * bloom * 0.10 * (0.3 + speedNorm);

    return vec4<f32>(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
