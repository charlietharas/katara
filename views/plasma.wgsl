fn densityAt(coord: vec2<i32>, densityTexture: texture_2d<f32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(densityTexture, clamped, 0).r;
}

fn velocityAt(coord: vec2<i32>, velocityTexture: texture_2d<f32>) -> vec2<f32> {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(velocityTexture, clamped, 0).xy;
}

fn plasmaPalette(t: f32) -> vec3<f32> {
    let indigo = vec3<f32>(0.18, 0.08, 0.32);
    let purple = vec3<f32>(0.52, 0.12, 0.58);
    let pink = vec3<f32>(0.97, 0.20, 0.55);
    let redOrange = vec3<f32>(1.0, 0.38, 0.24);
    let orange = vec3<f32>(1.0, 0.58, 0.16);
    let yellow = vec3<f32>(1.0, 0.88, 0.42);

    let x = clamp(t, 0.0, 1.0);
    var color = mix(indigo, purple, smoothstep(0.0, 0.22, x));
    color = mix(color, pink, smoothstep(0.22, 0.45, x));
    color = mix(color, redOrange, smoothstep(0.45, 0.65, x));
    color = mix(color, orange, smoothstep(0.65, 0.82, x));
    color = mix(color, yellow, smoothstep(0.82, 1.0, x));
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
    let gradMag = length(grad);

    let velocity = velocityAt(texCoord, velocityTexture);

    let densityAmt = clamp(density * 1.5, 0.0, 1.0);
    let warp = velocity * 7.0 + grad * 4.0;
    let marble = sin(warp.x * 1.0 + warp.y * 0.8 + density * 4.0) * 0.5 + 0.5;
    let t = clamp(densityAmt * 0.8 + marble * 0.2 * densityAmt, 0.0, 1.0);

    var color = plasmaPalette(t);

    let edge = smoothstep(0.06, 0.22, gradMag);
    color = color + vec3<f32>(1.0, 0.92, 0.8) * edge * 0.12;

    return vec4<f32>(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
