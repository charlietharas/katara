fn pressureAt(coord: vec2<i32>, pressureTexture: texture_2d<f32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(pressureTexture, clamped, 0).r;
}

fn densityAt(coord: vec2<i32>, densityTexture: texture_2d<f32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(densityTexture, clamped, 0).r;
}

fn doodleIsobarPalette(t: f32) -> vec3<f32> {
    let teal = vec3<f32>(0.10, 0.45, 0.50);
    let blue = vec3<f32>(0.12, 0.28, 0.62);
    let purple = vec3<f32>(0.42, 0.24, 0.55);
    let red = vec3<f32>(0.72, 0.18, 0.14);
    let orange = vec3<f32>(0.85, 0.42, 0.10);

    let x = clamp(t, 0.0, 1.0);
    var color = mix(teal, blue, smoothstep(0.0, 0.30, x));
    color = mix(color, purple, smoothstep(0.30, 0.50, x));
    color = mix(color, red, smoothstep(0.50, 0.72, x));
    color = mix(color, orange, smoothstep(0.72, 1.0, x));
    return color;
}

#draw
    let solid = textureLoad(solidTexture, texCoord, 0).r;
    if (solid <= 0.5) {
        return vec4<f32>(0.0);
    }

    var color = vec3<f32>(0.91, 0.88, 0.79);
    let gx = fract(f32(texCoord.x) / 40.0);
    let gy = fract(f32(texCoord.y) / 40.0);
    let graticule = min(min(gx, 1.0 - gx), min(gy, 1.0 - gy)) * 40.0;
    color = color - vec3<f32>(0.05, 0.06, 0.04)
        * (1.0 - smoothstep(0.4, 0.9, graticule));

    let rangeMin = uniforms.pressureMin;
    let rangeMax = max(uniforms.pressureMax, rangeMin + 1e-6);
    let pressureHere = (pressureAt(texCoord, pressureTexture) - rangeMin)
        / (rangeMax - rangeMin);
    let left = (pressureAt(texCoord + vec2<i32>(-1, 0), pressureTexture) - rangeMin)
        / (rangeMax - rangeMin);
    let right = (pressureAt(texCoord + vec2<i32>(1, 0), pressureTexture) - rangeMin)
        / (rangeMax - rangeMin);
    let top = (pressureAt(texCoord + vec2<i32>(0, 1), pressureTexture) - rangeMin)
        / (rangeMax - rangeMin);
    let bottom = (pressureAt(texCoord + vec2<i32>(0, -1), pressureTexture) - rangeMin)
        / (rangeMax - rangeMin);
    let pressureGrad = max(length(vec2<f32>(right - left, top - bottom)), 1e-4);

    let washMid = smoothstep(0.30, 0.50, pressureHere)
        - smoothstep(0.50, 0.70, pressureHere);
    color = color + vec3<f32>(-0.02, 0.01, 0.03)
        * (1.0 - smoothstep(0.15, 0.5, pressureHere)) * 1.2;
    color = color + vec3<f32>(0.035, 0.005, -0.02)
        * smoothstep(0.5, 0.85, pressureHere) * 1.2;
    color = color - vec3<f32>(0.01, 0.0, 0.01) * washMid;

    const SPACING: f32 = 0.08;
    let contourPos = pressureHere / SPACING;
    let bandIndex = i32(floor(contourPos));
    let contourFrac = fract(contourPos);
    let contourDist = min(contourFrac, 1.0 - contourFrac) * SPACING;

    let isIndex = (bandIndex % 5) == 0 || (bandIndex % 5) == -4;
    let widthMul = select(0.55, 1.15, isIndex);
    let lineW = pressureGrad * widthMul;
    let isobar = 1.0 - smoothstep(lineW * 0.5, lineW * 1.6, contourDist);
    let alphaMul = select(0.55, 0.95, isIndex);

    let pressureColor = clamp((pressureHere - 0.5) * 1.7 + 0.5, 0.0, 1.0);
    color = mix(color, doodleIsobarPalette(pressureColor), isobar * alphaMul);

    let density = clamp(densityAt(texCoord, densityTexture), 0.0, 1.0);
    if (density > 0.01) {
        let densityLeft = densityAt(texCoord + vec2<i32>(-1, 0), densityTexture);
        let densityRight = densityAt(texCoord + vec2<i32>(1, 0), densityTexture);
        let densityTop = densityAt(texCoord + vec2<i32>(0, 1), densityTexture);
        let densityBottom = densityAt(texCoord + vec2<i32>(0, -1), densityTexture);
        let densityGrad = max(
            length(vec2<f32>(densityRight - densityLeft, densityTop - densityBottom)),
            1e-3
        );

        const D_SPACING: f32 = 0.15;
        let densityPos = density / D_SPACING;
        let densityFrac = fract(densityPos);
        let densityDist = min(densityFrac, 1.0 - densityFrac) * D_SPACING;
        let densityLine = 1.0 - smoothstep(
            densityGrad * 0.5,
            densityGrad * 1.6,
            densityDist
        );

        let sepia = vec3<f32>(0.45, 0.28, 0.12);
        color = mix(
            color,
            sepia,
            densityLine * smoothstep(0.02, 0.1, density) * 0.6
        );
    }

    return vec4<f32>(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
