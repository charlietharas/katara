fn densityAt(coord: vec2<i32>, densityTexture: texture_2d<f32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(densityTexture, clamped, 0).r;
}

fn densityBlur3x3(coord: vec2<i32>, densityTexture: texture_2d<f32>) -> f32 {
    var sum = 0.0;
    for (var oy = -1; oy <= 1; oy++) {
        for (var ox = -1; ox <= 1; ox++) {
            sum += densityAt(coord + vec2<i32>(ox, oy), densityTexture);
        }
    }
    return sum / 9.0;
}

fn velocityAt(coord: vec2<i32>, velocityTexture: texture_2d<f32>) -> vec2<f32> {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(velocityTexture, clamped, 0).xy;
}

fn shimmerPalette(t: f32) -> vec3<f32> {
    let deepTeal = vec3<f32>(0.05, 0.14, 0.18);
    let darkTeal = vec3<f32>(0.06, 0.22, 0.32);
    let cyan = vec3<f32>(0.16, 0.80, 0.86);
    let blue = vec3<f32>(0.34, 0.46, 0.96);
    let violet = vec3<f32>(0.62, 0.33, 0.92);
    let pink = vec3<f32>(0.96, 0.44, 0.80);

    let x = clamp(t, 0.0, 1.0);
    var color = mix(deepTeal, darkTeal, smoothstep(0.0, 0.15, x));
    color = mix(color, cyan, smoothstep(0.15, 0.38, x));
    color = mix(color, blue, smoothstep(0.38, 0.58, x));
    color = mix(color, violet, smoothstep(0.58, 0.78, x));
    color = mix(color, pink, smoothstep(0.78, 1.0, x));
    return color;
}

#draw
    let solid = textureLoad(solidTexture, texCoord, 0).r;
    if (solid <= 0.5) {
        return vec4<f32>(0.0);
    }

    let pressure = textureLoad(pressureTexture, texCoord, 0).r;
    let densityBlur = densityBlur3x3(texCoord, densityTexture);

    let left = densityBlur3x3(texCoord + vec2<i32>(-1, 0), densityTexture);
    let right = densityBlur3x3(texCoord + vec2<i32>(1, 0), densityTexture);
    let top = densityBlur3x3(texCoord + vec2<i32>(0, 1), densityTexture);
    let bottom = densityBlur3x3(texCoord + vec2<i32>(0, -1), densityTexture);
    let grad = vec2<f32>(right - left, top - bottom);
    let gradMag = length(grad);

    let velocity = velocityAt(texCoord, velocityTexture);

    let warp = velocity * 6.0 + grad * 3.0;
    let phase = sin(
        warp.x * 0.9 + warp.y * 0.7 + densityBlur * 3.0 + pressure * 1.0
    ) + sin(
        warp.x * -0.7 + warp.y * 1.0 + length(warp) * 0.7 + densityBlur * 2.0
    );
    let phaseN = phase * 0.25;

    let densityAmt = clamp(densityBlur * 1.5, 0.0, 1.0);
    let t = clamp(0.15 + densityAmt * 0.5 + phaseN * densityAmt, 0.0, 1.0);

    var color = shimmerPalette(t);

    let edge = smoothstep(0.04, 0.2, gradMag);
    color = color + vec3<f32>(0.85, 0.92, 1.0) * edge * 0.1;

    return vec4<f32>(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
