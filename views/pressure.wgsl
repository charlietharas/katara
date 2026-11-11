#draw
    let solid = textureLoad(solidTexture, texCoord, 0).r;
    if (solid <= 0.5) {
        return vec4<f32>(0.0);
    }

    let pressure = textureLoad(pressureTexture, texCoord, 0).r;
    let rangeMin = uniforms.pressureMin;
    let rangeMax = uniforms.pressureMax;
    let clampedValue = clamp(pressure, rangeMin, rangeMax - 0.0001);
    let delta = rangeMax - rangeMin;
    let normalized = select(0.5, (clampedValue - rangeMin) / delta, delta != 0.0);

    let segmentSize = 0.25;
    let segment = i32(normalized / segmentSize);
    let segmentT = (normalized - f32(segment) * segmentSize) / segmentSize;

    var color = vec3<f32>(0.0, 0.0, 0.0);
    switch (segment) {
        case 0: { color = vec3<f32>(0.0, segmentT, 1.0); break; }
        case 1: { color = vec3<f32>(0.0, 1.0, 1.0 - segmentT); break; }
        case 2: { color = vec3<f32>(segmentT, 1.0, 0.0); break; }
        case 3: { color = vec3<f32>(1.0, 1.0 - segmentT, 0.0); break; }
        default: { color = vec3<f32>(1.0, 0.0, 0.0); break; }
    }

    return vec4<f32>(color, 1.0);
