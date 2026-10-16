#draw
    let solid = textureLoad(solidTexture, texCoord, 0).r;
    if (solid <= 0.5) {
        return vec4<f32>(0.0);
    }

    return vec4<f32>(textureLoad(inkTexture, texCoord, 0).rgb, 1.0);
