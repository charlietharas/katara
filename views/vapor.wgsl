fn densityAt(coord: vec2<i32>, densityTexture: texture_2d<f32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(densityTexture, clamped, 0).r;
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
    let edge = abs(density - left) + abs(density - right)
        + abs(density - top) + abs(density - bottom);
    let mist = smoothstep(0.015, 0.18, edge) * smoothstep(0.05, 0.85, density);
    let base = vec3<f32>(0.04, 0.05, 0.08);
    let fog = vec3<f32>(0.62, 0.76, 0.9) * mist * 0.8;
    let haze = vec3<f32>(0.82, 0.86, 0.94) * density * 0.22;
    let color = clamp(base + fog + haze, vec3<f32>(0.0), vec3<f32>(1.0));
    return vec4<f32>(color, 1.0);
