fn densityAt(coord: vec2<i32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(densityTexture, clamped, 0).r;
}

fn draw(texCoord: vec2<i32>, pressure: f32, density: f32) -> vec3<f32> {
    let left = densityAt(texCoord + vec2<i32>(-1, 0));
    let right = densityAt(texCoord + vec2<i32>(1, 0));
    let top = densityAt(texCoord + vec2<i32>(0, 1));
    let bottom = densityAt(texCoord + vec2<i32>(0, -1));
    let edge = abs(density - left) + abs(density - right) + abs(density - top) + abs(density - bottom);
    let mist = smoothstep(0.015, 0.18, edge) * smoothstep(0.05, 0.85, density);
    let base = vec3<f32>(0.04, 0.05, 0.08);
    let fog = vec3<f32>(0.62, 0.76, 0.9) * mist * 0.8;
    let haze = vec3<f32>(0.82, 0.86, 0.94) * density * 0.22;
    return clamp(base + fog + haze, vec3<f32>(0.0), vec3<f32>(1.0));
}
