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

    let left = densityAt(texCoord + vec2<i32>(-1, 0), densityTexture);
    let right = densityAt(texCoord + vec2<i32>(1, 0), densityTexture);
    let top = densityAt(texCoord + vec2<i32>(0, 1), densityTexture);
    let bottom = densityAt(texCoord + vec2<i32>(0, -1), densityTexture);

    let dx = right - left;
    let dy = top - bottom;
    let normal = normalize(vec3<f32>(-dx * 4.0, -dy * 4.0, 1.0));
    let lightDir = normalize(vec3<f32>(0.45, -0.55, 0.7));
    let diffuse = max(dot(normal, lightDir), 0.0);
    let intensity = 0.2 + 0.8 * diffuse;
    return vec4<f32>(intensity, intensity, intensity, 1.0);
