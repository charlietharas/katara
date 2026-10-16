fn centeredVelocityAt(coord: vec2<i32>, velocityTexture: texture_2d<f32>) -> vec2<f32> {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    let iRight = min(clamped.x + 1, uniforms.gridX - 1);
    let jUp = min(clamped.y + 1, uniforms.gridY - 1);
    let velHere = textureLoad(velocityTexture, clamped, 0).xy;
    let velRight = textureLoad(velocityTexture, vec2<i32>(iRight, clamped.y), 0).xy;
    let velUp = textureLoad(velocityTexture, vec2<i32>(clamped.x, jUp), 0).xy;
    return 0.5 * vec2<f32>(velHere.x + velRight.x, velHere.y + velUp.y);
}

#draw
    let solid = textureLoad(solidTexture, texCoord, 0).r;
    if (solid <= 0.5) {
        return vec4<f32>(0.0);
    }

    let velRight = centeredVelocityAt(texCoord + vec2<i32>(1, 0), velocityTexture);
    let velLeft = centeredVelocityAt(texCoord + vec2<i32>(-1, 0), velocityTexture);
    let velTop = centeredVelocityAt(texCoord + vec2<i32>(0, 1), velocityTexture);
    let velBottom = centeredVelocityAt(texCoord + vec2<i32>(0, -1), velocityTexture);
    let dvdx = (velRight.y - velLeft.y) * 0.5;
    let dudy = (velTop.x - velBottom.x) * 0.5;
    let curl = dvdx - dudy;

    let magnitude = abs(curl);
    let response = smoothstep(0.002, 0.05, magnitude)
        * (1.0 - exp(-magnitude * 30.0));

    let clockwise = vec3<f32>(0.94, 0.25, 0.22);
    let counterClockwise = vec3<f32>(0.15, 0.75, 0.72);
    let isClockwise = 0.0 > curl;
    let inkColor = select(clockwise, counterClockwise, isClockwise);

    let density = textureLoad(densityTexture, texCoord, 0).r;
    var color = vec3<f32>(0.012, 0.014, 0.03)
        + vec3<f32>(0.02, 0.03, 0.05) * clamp(density, 0.0, 1.0);
    color = color + inkColor * response;

    let core = smoothstep(0.55, 0.95, response);
    color = color + vec3<f32>(0.9, 0.92, 0.95) * core * 0.35;

    return vec4<f32>(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
