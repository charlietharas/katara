fn draw(texCoord: vec2<i32>, pressure: f32, density: f32) -> vec3<f32> {
    return textureLoad(inkTexture, texCoord, 0).rgb;
}
