fn heatmapPalette(t: f32) -> vec3<f32> {
    let s = clamp(t, 0.0, 1.0);
    if (s < 0.25) {
        let k = s / 0.25;
        return mix(vec3<f32>(0.02, 0.0, 0.05), vec3<f32>(0.55, 0.0, 0.0), k);
    }
    if (s < 0.5) {
        let k = (s - 0.25) / 0.25;
        return mix(vec3<f32>(0.55, 0.0, 0.0), vec3<f32>(1.0, 0.16, 0.0), k);
    }
    if (s < 0.75) {
        let k = (s - 0.5) / 0.25;
        return mix(vec3<f32>(1.0, 0.16, 0.0), vec3<f32>(1.0, 0.72, 0.0), k);
    }
    let k = (s - 0.75) / 0.25;
    return mix(vec3<f32>(1.0, 0.72, 0.0), vec3<f32>(1.0, 0.98, 0.88), k);
}

#draw
    let solid = textureLoad(solidTexture, texCoord, 0).r;
    if (solid <= 0.5) {
        return vec4<f32>(0.0);
    }

    let density = textureLoad(densityTexture, texCoord, 0).r;
    let t = clamp(1.0 - density, 0.0, 1.0);
    let steps = 11.0;
    let q = floor(t * steps) / steps;
    return vec4<f32>(heatmapPalette(q), 1.0);
