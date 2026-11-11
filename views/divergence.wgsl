fn divergenceVelocityAt(coord: vec2<i32>, velocityTexture: texture_2d<f32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    let center = textureLoad(velocityTexture, clamped, 0).xy;
    let right = textureLoad(velocityTexture, vec2<i32>(
        clamp(coord.x + 1, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    ), 0).xy;
    let top = textureLoad(velocityTexture, vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y + 1, 0, uniforms.gridY - 1)
    ), 0).xy;
    let bottom = textureLoad(velocityTexture, vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y - 1, 0, uniforms.gridY - 1)
    ), 0).xy;
    return right.x - center.x + top.y - bottom.y;
}

fn divergenceSmoothAt(coord: vec2<i32>, velocityTexture: texture_2d<f32>) -> f32 {
    var sum = divergenceVelocityAt(coord, velocityTexture) * 4.0;
    sum += divergenceVelocityAt(coord + vec2<i32>(1, 0), velocityTexture) * 2.0;
    sum += divergenceVelocityAt(coord + vec2<i32>(-1, 0), velocityTexture) * 2.0;
    sum += divergenceVelocityAt(coord + vec2<i32>(0, 1), velocityTexture) * 2.0;
    sum += divergenceVelocityAt(coord + vec2<i32>(0, -1), velocityTexture) * 2.0;
    sum += divergenceVelocityAt(coord + vec2<i32>(1, 1), velocityTexture) * 1.0;
    sum += divergenceVelocityAt(coord + vec2<i32>(1, -1), velocityTexture) * 1.0;
    sum += divergenceVelocityAt(coord + vec2<i32>(-1, 1), velocityTexture) * 1.0;
    sum += divergenceVelocityAt(coord + vec2<i32>(-1, -1), velocityTexture) * 1.0;
    return sum / 16.0;
}

fn divergenceDisplayAt(coord: vec2<i32>, velocityTexture: texture_2d<f32>) -> f32 {
    if (DIVERGENCE_BINOMIAL_FILTERING) {
        return divergenceSmoothAt(coord, velocityTexture);
    }
    return divergenceVelocityAt(coord, velocityTexture);
}

fn divergenceLocalScale(coord: vec2<i32>, velocityTexture: texture_2d<f32>) -> f32 {
    var maxAbsDiv = abs(divergenceDisplayAt(coord, velocityTexture));
    maxAbsDiv = max(
        maxAbsDiv,
        abs(divergenceDisplayAt(coord + vec2<i32>(1, 0), velocityTexture))
    );
    maxAbsDiv = max(
        maxAbsDiv,
        abs(divergenceDisplayAt(coord + vec2<i32>(-1, 0), velocityTexture))
    );
    maxAbsDiv = max(
        maxAbsDiv,
        abs(divergenceDisplayAt(coord + vec2<i32>(0, 1), velocityTexture))
    );
    maxAbsDiv = max(
        maxAbsDiv,
        abs(divergenceDisplayAt(coord + vec2<i32>(0, -1), velocityTexture))
    );
    return max(maxAbsDiv, 1e-4);
}

#draw
    let solid = textureLoad(solidTexture, texCoord, 0).r;
    if (solid <= 0.5) {
        return vec4<f32>(0.0);
    }

    let divergence = divergenceDisplayAt(texCoord, velocityTexture);
    let divergenceScale = divergenceLocalScale(texCoord, velocityTexture);

    let normalized = clamp(divergence / divergenceScale, -1.0, 1.0);
    let magnitude = pow(abs(normalized), 0.65);
    let base = vec3<f32>(0.02, 0.02, 0.025);
    let negColor = vec3<f32>(0.12, 0.38, 1.0);
    let posColor = vec3<f32>(1.0, 0.24, 0.14);
    let signedColor = select(negColor, posColor, normalized >= 0.0);
    let color = clamp(base + signedColor * magnitude, vec3<f32>(0.0), vec3<f32>(1.0));
    return vec4<f32>(color, 1.0);
