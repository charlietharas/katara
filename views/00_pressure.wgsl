fn draw(texCoord: vec2<i32>, pressure: f32, density: f32) -> vec3<f32> {
    let value = pressure;
    let rangeMin = uniforms.pressureMin;
    let rangeMax = uniforms.pressureMax;
    let clampedValue = clamp(value, rangeMin, rangeMax - 0.0001);
    let delta = rangeMax - rangeMin;
    let normalized = select(0.5, (clampedValue - rangeMin) / delta, delta != 0.0);

    let m = 0.25;
    let num = i32(normalized / m);
    let s = (normalized - f32(num) * m) / m;

    var color = vec3<f32>(0.0, 0.0, 0.0);

    switch(num) {
        case 0: { color = vec3<f32>(0.0, s, 1.0); break; }
        case 1: { color = vec3<f32>(0.0, 1.0, 1.0 - s); break; }
        case 2: { color = vec3<f32>(s, 1.0, 0.0); break; }
        case 3: { color = vec3<f32>(1.0, 1.0 - s, 0.0); break; }
        default: { color = vec3<f32>(1.0, 0.0, 0.0); break; }
    }

    return color;
}
