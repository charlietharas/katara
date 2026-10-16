#draw
    let solid = textureLoad(solidTexture, texCoord, 0).r;
    if (solid <= 0.5) {
        return vec4<f32>(0.0);
    }

    let density = textureLoad(densityTexture, texCoord, 0).r;
    let grey = clamp(density, 0.0, 1.0);
    return vec4<f32>(grey, grey, grey, 1.0);
