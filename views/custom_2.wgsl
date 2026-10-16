#draw
    let checker = ((texCoord.x ^ texCoord.y) & 8) != 0;
    let color = select(vec3<f32>(1.0, 0.0, 1.0), vec3<f32>(0.0, 0.0, 0.0), checker);
    return vec4<f32>(color, 1.0);
