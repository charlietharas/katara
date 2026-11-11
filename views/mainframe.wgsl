fn hashNoise(coord: vec2<i32>) -> f32 {
    let h = dot(
        vec2<f32>(f32(coord.x), f32(coord.y)),
        vec2<f32>(127.1, 311.7)
    );
    return fract(sin(h) * 43758.5453);
}

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

fn solidAt(coord: vec2<i32>, solidTexture: texture_2d<f32>) -> f32 {
    let clamped = vec2<i32>(
        clamp(coord.x, 0, uniforms.gridX - 1),
        clamp(coord.y, 0, uniforms.gridY - 1)
    );
    return textureLoad(solidTexture, clamped, 0).r;
}

fn mainframePalette(t: f32) -> vec3<f32> {
    let voidGreen = vec3<f32>(0.004, 0.018, 0.008);
    let deepGreen = vec3<f32>(0.04, 0.19, 0.08);
    let phosphor = vec3<f32>(0.35, 0.80, 0.40);
    let paleGreen = vec3<f32>(0.85, 0.98, 0.85);
    let x = clamp(t, 0.0, 1.0);
    var color = mix(voidGreen, deepGreen, smoothstep(0.0, 0.35, x));
    color = mix(color, phosphor, smoothstep(0.35, 0.7, x));
    color = mix(color, paleGreen, smoothstep(0.7, 1.0, x));
    return color;
}

#draw
    let solid = textureLoad(solidTexture, texCoord, 0).r;
    if (solid <= 0.5) {
        return vec4<f32>(0.0);
    }

    const STEPS: i32 = 10;

    var pos = vec2<f32>(f32(texCoord.x), f32(texCoord.y));

    let vDrift = centeredVelocityAt(texCoord, velocityTexture);
    let driftSpeed = length(vDrift);
    let drift = vec2<i32>(
        i32(round(vDrift.x * 6.0)),
        i32(round(vDrift.y * 6.0))
    );

    let stepLen = 0.6 + 1.8 * smoothstep(0.05, 1.2, driftSpeed);

    var sum = hashNoise(texCoord + drift);
    var weight = 1.0;

    var p = pos;
    for (var i = 0; i < STEPS; i++) {
        let v = centeredVelocityAt(vec2<i32>(p), velocityTexture);
        let speed = length(v);
        if (speed < 1e-6) {
            break;
        }
        p = p - (v / speed) * stepLen;
        let cell = vec2<i32>(i32(p.x), i32(p.y));
        if (solidAt(cell, solidTexture) <= 0.5) {
            break;
        }
        sum = sum + hashNoise(cell + drift);
        weight = weight + 1.0;
    }

    p = pos;
    for (var i = 0; i < STEPS; i++) {
        let v = centeredVelocityAt(vec2<i32>(p), velocityTexture);
        let speed = length(v);
        if (speed < 1e-6) {
            break;
        }
        p = p + (v / speed) * stepLen;
        let cell = vec2<i32>(i32(p.x), i32(p.y));
        if (solidAt(cell, solidTexture) <= 0.5) {
            break;
        }
        sum = sum + hashNoise(cell + drift);
        weight = weight + 1.0;
    }

    let lic = sum / weight;

    let fiber = pow(clamp((lic - 0.30) * 2.6, 0.0, 1.0), 1.7);

    let vHere = centeredVelocityAt(texCoord, velocityTexture);
    let speedHere = length(vHere);
    let flowT = smoothstep(0.06, 1.2, speedHere);

    let presence = smoothstep(0.01, 0.08, speedHere);

    let brightness = fiber * presence * (0.25 + 0.75 * flowT);

    let angle = atan2(vHere.y, vHere.x);
    let dirWheel = vec3<f32>(
        0.10 + 0.20 * cos(angle),
        0.65 + 0.35 * cos(angle - 2.094),
        0.40 + 0.30 * cos(angle - 4.189)
    );

    var color = mainframePalette(brightness);
    color = color + dirWheel * brightness * brightness * flowT * 0.9;

    let density = textureLoad(densityTexture, texCoord, 0).r;
    color = color + vec3<f32>(0.08, 0.22, 0.08) * smoothstep(0.3, 0.9, density);

    return vec4<f32>(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
