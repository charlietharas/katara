fn draw(texCoord: vec2<i32>, pressure: f32, density: f32) -> vec3<f32> {
    let value = density;
    let rangeMin = 0.0;
    let rangeMax = 1.0;
    let t = (value - rangeMin) / (rangeMax - rangeMin);
    let grey = clamp(t, 0.0, 1.0);
    return vec3<f32>(grey, grey, grey);
}
