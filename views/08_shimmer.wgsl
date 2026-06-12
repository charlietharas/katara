fn densityAt(coord: vec2<i32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(densityTexture, clamped, 0).r;
}

fn densityBlur3(coord: vec2<i32>) -> f32 {
    var sum = 0.0;
    for (var oy = -1; oy <= 1; oy++) {
        for (var ox = -1; ox <= 1; ox++) {
            sum += densityAt(coord + vec2<i32>(ox, oy));
        }
    }
    return sum / 9.0;
}

fn velAt(coord: vec2<i32>) -> vec2<f32> {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(velocityTexture, clamped, 0).xy;
}

// Oil-slick ramp. The low end (t ~ 0.15) is the calm empty-cell background, so
// there is no separate dark "void" color; fluid lives in the bright band above
// it (cyan -> blue -> violet -> pink) and therefore never reads as black.
fn shimmerPalette(t: f32) -> vec3<f32> {
    let deepTeal = vec3<f32>(0.05, 0.14, 0.18);
    let darkTeal = vec3<f32>(0.06, 0.22, 0.32);
    let cyan = vec3<f32>(0.16, 0.80, 0.86);
    let blue = vec3<f32>(0.34, 0.46, 0.96);
    let violet = vec3<f32>(0.62, 0.33, 0.92);
    let pink = vec3<f32>(0.96, 0.44, 0.80);

    let x = clamp(t, 0.0, 1.0);
    var c = mix(deepTeal, darkTeal, smoothstep(0.0, 0.15, x));
    c = mix(c, cyan, smoothstep(0.15, 0.38, x));
    c = mix(c, blue, smoothstep(0.38, 0.58, x));
    c = mix(c, violet, smoothstep(0.58, 0.78, x));
    c = mix(c, pink, smoothstep(0.78, 1.0, x));
    return c;
}

fn draw(texCoord: vec2<i32>, pressure: f32, density: f32) -> vec3<f32> {
    let densityBlur = densityBlur3(texCoord);

    let left = densityBlur3(texCoord + vec2<i32>(-1, 0));
    let right = densityBlur3(texCoord + vec2<i32>(1, 0));
    let top = densityBlur3(texCoord + vec2<i32>(0, 1));
    let bottom = densityBlur3(texCoord + vec2<i32>(0, -1));
    let grad = vec2<f32>(right - left, top - bottom);
    let gradMag = length(grad);

    let vel = velAt(texCoord);

    // Low-frequency warp so neighbouring cells vary smoothly (no noisy /
    // discontinuous fragments). Driven purely by the fluid.
    let warp = vel * 6.0 + grad * 3.0;
    let phase = sin(warp.x * 0.9 + warp.y * 0.7 + densityBlur * 3.0 + pressure * 1.0)
              + sin(warp.x * -0.7 + warp.y * 1.0 + length(warp) * 0.7 + densityBlur * 2.0);
    let phaseN = phase * 0.25;

    // Empty cells (dn = 0) -> t = 0.15 -> uniform calm background. Fluid pushes
    // into the bright band and the phase makes colors shimmer within it.
    let dn = clamp(densityBlur * 1.5, 0.0, 1.0);
    let t = clamp(0.15 + dn * 0.5 + phaseN * dn, 0.0, 1.0);

    var color = shimmerPalette(t);

    // Faint sheen on moving edges only; kept subtle so it never looks tacky.
    let edge = smoothstep(0.04, 0.2, gradMag);
    color = color + vec3<f32>(0.85, 0.92, 1.0) * edge * 0.1;

    return clamp(color, vec3<f32>(0.0), vec3<f32>(1.0));
}
