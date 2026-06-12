fn densityAt(coord: vec2<i32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(densityTexture, clamped, 0).r;
}

fn chromeHeatmap(value: f32, min: f32, max: f32) -> vec3<f32> {
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
    let left = densityAt(texCoord + vec2<i32>(-1, 0));
    let right = densityAt(texCoord + vec2<i32>(1, 0));
    let top = densityAt(texCoord + vec2<i32>(0, 1));
    let bottom = densityAt(texCoord + vec2<i32>(0, -1));
    let dx = right - left;
    let dy = top - bottom;
    let normal = normalize(vec3<f32>(-dx * 3.5, -dy * 3.5, 1.0));
    let lightDir = normalize(vec3<f32>(0.35, -0.55, 0.75));
    let viewDir = vec3<f32>(0.0, 0.0, 1.0);
    let reflectDir = reflect(-viewDir, normal);
    let hue = fract(reflectDir.x * 0.45 + reflectDir.y * 0.35 + density * 0.75);
    let rainbow = chromeHeatmap(hue, 0.0, 1.0);
    let diffuse = max(dot(normal, lightDir), 0.0);
    let halfDir = normalize(lightDir + viewDir);
    let spec = pow(max(dot(normal, halfDir), 0.0), 18.0);
    return rainbow * (0.28 + 0.72 * density) * (0.35 + 0.65 * diffuse) + vec3<f32>(spec * 0.85);
}
