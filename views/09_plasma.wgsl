fn densityAt(coord: vec2<i32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(densityTexture, clamped, 0).r;
}

fn velAt(coord: vec2<i32>) -> vec2<f32> {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(velocityTexture, clamped, 0).xy;
}

// Monotonic vaporwave sunset ramp: indigo -> purple -> hot pink -> orange ->
// yellow. Low end doubles as the empty-cell background so there is no separate
// dark "void" color and the gradient stays continuous.
fn vaporwavePalette(t: f32) -> vec3<f32> {
    let indigo = vec3<f32>(0.18, 0.08, 0.32);
    let purple = vec3<f32>(0.52, 0.12, 0.58);
    let pink = vec3<f32>(0.97, 0.20, 0.55);
    let redOrange = vec3<f32>(1.0, 0.38, 0.24);
    let orange = vec3<f32>(1.0, 0.58, 0.16);
    let yellow = vec3<f32>(1.0, 0.88, 0.42);

    let x = clamp(t, 0.0, 1.0);
    var c = mix(indigo, purple, smoothstep(0.0, 0.22, x));
    c = mix(c, pink, smoothstep(0.22, 0.45, x));
    c = mix(c, redOrange, smoothstep(0.45, 0.65, x));
    c = mix(c, orange, smoothstep(0.65, 0.82, x));
    c = mix(c, yellow, smoothstep(0.82, 1.0, x));
    return c;
}

fn draw(texCoord: vec2<i32>, pressure: f32, density: f32) -> vec3<f32> {
    let left = densityAt(texCoord + vec2<i32>(-1, 0));
    let right = densityAt(texCoord + vec2<i32>(1, 0));
    let top = densityAt(texCoord + vec2<i32>(0, 1));
    let bottom = densityAt(texCoord + vec2<i32>(0, -1));
    let grad = vec2<f32>(right - left, top - bottom);
    let gradMag = length(grad);

    let vel = velAt(texCoord);

    // Density drives the gradient position; a low-frequency flow term adds gentle
    // marbled texture only where there is fluid (so empty cells stay uniform).
    let dn = clamp(density * 1.5, 0.0, 1.0);
    let warp = vel * 7.0 + grad * 4.0;
    let tex = sin(warp.x * 1.0 + warp.y * 0.8 + density * 4.0) * 0.5 + 0.5;
    let t = clamp(dn * 0.8 + tex * 0.2 * dn, 0.0, 1.0);

    var color = vaporwavePalette(t);

    // Post kept minimal: a faint warm sheen on edges only.
    let edge = smoothstep(0.06, 0.22, gradMag);
    color = color + vec3<f32>(1.0, 0.92, 0.8) * edge * 0.12;

    return clamp(color, vec3<f32>(0.0), vec3<f32>(1.0));
}
