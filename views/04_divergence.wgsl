fn divergenceVelocityAt(coord: vec2<i32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    let center = textureLoad(velocityTexture, clamped, 0).rg;
    let right = textureLoad(velocityTexture, vec2<i32>(
        clamp(coord.x + 1, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    ), 0).rg;
    let top = textureLoad(velocityTexture, vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y + 1, 0, uniforms.gridY - 1)
    ), 0).rg;
    let bottom = textureLoad(velocityTexture, vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y - 1, 0, uniforms.gridY - 1)
    ), 0).rg;
    return right.x - center.x + top.y - bottom.y;
}

fn divergenceLocalScale(coord: vec2<i32>) -> f32 {
    var maxAbsDiv = abs(divergenceVelocityAt(coord));
    maxAbsDiv = max(maxAbsDiv, abs(divergenceVelocityAt(coord + vec2<i32>(1, 0))));
    maxAbsDiv = max(maxAbsDiv, abs(divergenceVelocityAt(coord + vec2<i32>(-1, 0))));
    maxAbsDiv = max(maxAbsDiv, abs(divergenceVelocityAt(coord + vec2<i32>(0, 1))));
    maxAbsDiv = max(maxAbsDiv, abs(divergenceVelocityAt(coord + vec2<i32>(0, -1))));
    return max(maxAbsDiv, 1e-4);
}

fn draw(texCoord: vec2<i32>, pressure: f32, density: f32) -> vec3<f32> {
    let divergence = divergenceVelocityAt(texCoord);
    let divergenceScale = divergenceLocalScale(texCoord);

    let normalized = clamp(divergence / divergenceScale, -1.0, 1.0);
    let magnitude = pow(abs(normalized), 0.65);
    let base = vec3<f32>(0.02, 0.02, 0.025);
    let negColor = vec3<f32>(0.12, 0.38, 1.0);
    let posColor = vec3<f32>(1.0, 0.24, 0.14);
    let signedColor = select(negColor, posColor, normalized >= 0.0);
    return clamp(base + signedColor * magnitude, vec3<f32>(0.0), vec3<f32>(1.0));
}
