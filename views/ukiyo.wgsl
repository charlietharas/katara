fn densityAt(coord: vec2<i32>, densityTexture: texture_2d<f32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(densityTexture, clamped, 0).r;
}

fn ukiyoBandColor(band: i32) -> vec3<f32> {
    switch (band) {
        case 0: { return vec3<f32>(0.93, 0.90, 0.82); }
        case 1: { return vec3<f32>(0.66, 0.78, 0.82); }
        case 2: { return vec3<f32>(0.30, 0.48, 0.62); }
        case 3: { return vec3<f32>(0.13, 0.24, 0.42); }
        default: { return vec3<f32>(0.05, 0.09, 0.19); }
    }
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

    const BANDS: f32 = 5.0;
    let bandPos = clamp(density, 0.0, 0.999) * BANDS;
    let band = i32(floor(bandPos));

    var color = ukiyoBandColor(band);

    let bandL = i32(floor(clamp(left, 0.0, 0.999) * BANDS));
    let bandR = i32(floor(clamp(right, 0.0, 0.999) * BANDS));
    let bandT = i32(floor(clamp(top, 0.0, 0.999) * BANDS));
    let bandB = i32(floor(clamp(bottom, 0.0, 0.999) * BANDS));
    let neighborChange = (bandL != band) || (bandR != band)
        || (bandT != band) || (bandB != band);

    let gradMag = length(grad);
    if (neighborChange && gradMag > 0.01) {
        let boundaryDensity = f32(band + 1) / BANDS;
        let distToBoundary = abs(density - boundaryDensity);
        let aa = max(gradMag, 0.02) * 1.1;
        let ink = 1.0 - smoothstep(aa * 0.5, aa * 1.5, distToBoundary);
        let wobble = 0.75 + 0.25 * sin(atan2(grad.y, grad.x) * 3.0);
        color = mix(color, vec3<f32>(0.04, 0.05, 0.08), ink * wobble);
    }

    let grain = fract(
        sin(dot(vec2<f32>(texCoord), vec2<f32>(12.9898, 78.233))) * 43758.5453
    );
    color = color + vec3<f32>((grain - 0.5) * 0.03);

    return vec4<f32>(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
