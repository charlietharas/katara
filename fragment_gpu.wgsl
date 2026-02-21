struct UniformData {
    drawTarget: i32,
    gridX: i32,
    gridY: i32,
    cellSize: f32,
    pressureMin: f32,
    pressureMax: f32,
    drawVelocities: i32,
    velScale: f32,
    windowWidth: f32,
    windowHeight: f32,
    simWidth: f32,
    simHeight: f32,
    disableHistograms: i32,
    densityHistogramMin: f32,
    densityHistogramMax: f32,
    densityHistogramMaxCount: i32,
    velocityHistogramMin: f32,
    velocityHistogramMax: f32,
    velocityHistogramMaxCount: i32,
    pad0: i32,
    densityHistogramBins: array<vec4<i32>, 16>,
    velocityHistogramBins: array<vec4<i32>, 16>,
};

@group(0) @binding(0) var<uniform> uniforms: UniformData;
@group(0) @binding(1) var pressureSampler: sampler;
@group(0) @binding(2) var pressureTexture: texture_2d<f32>;
@group(0) @binding(3) var densityTexture: texture_2d<f32>;
@group(0) @binding(4) var velocityTexture: texture_2d<f32>;
@group(0) @binding(5) var solidTexture: texture_2d<f32>;
@group(0) @binding(6) var inkTexture: texture_2d<f32>; // RGBA texture for GPU mode

// TODO MAIN -- WIP ^_^
// we in the browser baby

// TODO still some opportunities to clean up code

/*
// TODO examine differences in pressure/velocity histograms between GPU/CPU
- see plotting script
- note: it seems like we can get better behavior by cranking Jacobi iterations up (1000)
// TODO LATER swap to different pressure solver

// TODO up/right wind tunnel behaves differently from down/left
// TODO RELATED BUG WITH GPU HAND TRACKING
likely because of the forward texture accesses (e.g. asymmetric +1) somewhere
or it could be something else...
- there are still some subtle bugs with gpu sim (bot left corner, circle detection)
- and ofc the velocity field perturbations affecting the histogram limits when the circle is moved
- and general poor behavior at high magnitudes (e.g. of velocity)--are we correctly checking fluids at boundaries
  and is there any undefined behavior during advection?

// TODO WAY LATER - extensions:
- try to vibrate some obstacle proportional to an audio signal
- measure system entropy and write a proof that the system does/does not follow laws of fluid entropy
- 4 different views: raw camera with model visualization, ink, current pretty view, combined
 - combined: camera feed with model visualization rendered behind pretty fluid field

// TODO WAY LATER lots of tiny memory problems in valgrind, investigate if some of these are my fault
*/

// color helpers
fn mapValueToColor(value: f32, min: f32, max: f32) -> vec3<f32> {
    var clampedValue = clamp(value, min, max - 0.0001);
    var delta = max - min;
    var normalized = select(0.5, (clampedValue - min) / delta, delta != 0.0);

    var m = 0.25;
    var num = i32(normalized / m);
    var s = (normalized - f32(num) * m) / m;

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

fn mapValueToGreyscale(value: f32, min: f32, max: f32) -> vec3<f32> {
    var t = (value - min) / (max - min);
    t = clamp(t, 0.0, 1.0);
    return vec3<f32>(t, t, t);
}

fn mapValueToVelocityColor(value: f32, min: f32, max: f32) -> vec3<f32> {
    var clampedValue = clamp(value, min, max - 0.0001);
    var delta = max - min;
    var normalized = select(0.5, (clampedValue - min) / delta, delta != 0.0);

    if (normalized < 0.5) {
        var t = normalized * 2.0;
        return vec3<f32>(1.0, t * 0.647, 0.0); // orange to yellow
    } else {
        var t = (normalized - 0.5) * 2.0;
        return vec3<f32>(1.0, 0.647 + t * 0.353, 0.0); // yellow to white
    }
}

fn distanceToLineSegment(point: vec2<f32>, lineStart: vec2<f32>, lineEnd: vec2<f32>) -> f32 {
    var line = lineEnd - lineStart;
    var lineLength = length(line);

    if (lineLength < 0.0001) {
        return distance(point, lineStart);
    }

    var t = max(0.0, min(1.0, dot(point - lineStart, line) / (lineLength * lineLength)));
    var projection = lineStart + t * line;

    return distance(point, projection);
}

fn drawVelocityField(coord: vec2<f32>, gridX: i32, gridY: i32) -> vec4<f32> {
    // bounds
    if (gridX < 0 || gridX >= uniforms.gridX ||
        gridY < 0 || gridY >= uniforms.gridY) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    // load simulation data
    var solid = textureLoad(solidTexture, vec2<i32>(gridX, gridY), 0);
    var velocity = textureLoad(velocityTexture, vec2<i32>(gridX, gridY), 0);

    // only show velocity in fluid cells
    if (solid.r <= 0.5) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    var velX = velocity.x;
    var velY = velocity.y;
    
    // normalize velocity to fixed length
    var magnitude = sqrt(velX * velX + velY * velY);
    var normalizedLength = 0.3;
    if (magnitude > 0.001) {
        velX = (velX / magnitude) * normalizedLength;
        velY = (velY / magnitude) * normalizedLength;
    }

    // check if we're close enough to a velocity line to draw it
    var hLineStart = vec2<f32>(f32(gridX) * uniforms.cellSize, (f32(gridY) + 0.5) * uniforms.cellSize);
    var hLineEnd = vec2<f32>(hLineStart.x + velX * uniforms.velScale, hLineStart.y);
    var vLineStart = vec2<f32>((f32(gridX) + 0.5) * uniforms.cellSize, f32(gridY) * uniforms.cellSize);
    var vLineEnd = vec2<f32>(vLineStart.x, vLineStart.y - velY * uniforms.velScale);

    var lineWidth = 0.002;
    var color = vec3<f32>(0.0, 0.0, 0.0);

    // check distance to horizontal line
    if (abs(velX) > 0.001) {
        var hDist = distanceToLineSegment(coord, hLineStart, hLineEnd);
        if (hDist < lineWidth) {
            color = vec3<f32>(1.0, 1.0, 1.0);
        }
    }

    // check distance to vertical line
    if (abs(velY) > 0.001) {
        var vDist = distanceToLineSegment(coord, vLineStart, vLineEnd);
        if (vDist < lineWidth) {
            color = vec3<f32>(1.0, 1.0, 1.0);
        }
    }

    return vec4<f32>(color, 0.5);
}

fn getBinCount(vec: vec4<i32>, component: i32) -> i32 {
    if (component == 0) { return vec.x; }
    if (component == 1) { return vec.y; }
    if (component == 2) { return vec.z; }
    return vec.w;
}

fn drawHistograms(pixelCoord: vec2<f32>) -> vec4<f32> {
    if (uniforms.disableHistograms != 0) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    let histWidth = 300.0;
    let histHeight = 150.0;

    // density histogram
    let dhistX = 10.0;
    let dhistY = 10.0;
    // velocity histogram
    let vhistX = 320.0;
    let vhistY = 10.0;

    // draw density histogram
    if (pixelCoord.x >= dhistX && pixelCoord.x < dhistX + histWidth &&
        pixelCoord.y >= dhistY && pixelCoord.y < dhistY + histHeight) {

        let localX = pixelCoord.x - dhistX;
        let localY = pixelCoord.y - dhistY;

        // background
        let bg = 40.0 / 255.0;
        var result = vec3<f32>(bg, bg, bg);

        // border
        let border = 200.0 / 255.0;
        if (localX < 1.0 || localX >= histWidth - 1.0 || localY < 1.0 || localY >= histHeight - 1.0) {
            result = vec3<f32>(border, border, border);
        } else {
            // bar
            let barAreaX = localX - 10.0;
            let barAreaY = localY - 10.0;
            let barAreaWidth = histWidth - 20.0;
            let barAreaHeight = histHeight - 20.0;

            if (barAreaX >= 0.0 && barAreaX < barAreaWidth && barAreaY >= 0.0 && barAreaY < barAreaHeight) {
                let barWidth = histWidth / 64.0;
                var binIndex = i32(barAreaX / barWidth);
                binIndex = clamp(binIndex, 0, 63);

                let maxCount = uniforms.densityHistogramMaxCount;

                if (maxCount > 0) {
                    let vecIndex = binIndex / 4;
                    let component = binIndex % 4;
                    let vec = uniforms.densityHistogramBins[vecIndex];
                    let binCount = getBinCount(vec, component);
                    let barHeight = (f32(binCount) / f32(maxCount)) * barAreaHeight;
                    let barBottom = barAreaHeight - barHeight;

                    // within bar
                    if (barAreaY >= barBottom && barAreaY < barAreaHeight) {
                        let normalized = f32(binIndex) / 64.0;
                        result = mapValueToColor(normalized, 0.0, 1.0);
                    }
                }
            }
        }

        return vec4<f32>(result, 1.0);
    }

    // draw velocity histogram
    if (pixelCoord.x >= vhistX && pixelCoord.x < vhistX + histWidth &&
        pixelCoord.y >= vhistY && pixelCoord.y < vhistY + histHeight) {

        let localX = pixelCoord.x - vhistX;
        let localY = pixelCoord.y - vhistY;

        // background
        let bg = 40.0 / 255.0;
        var result = vec3<f32>(bg, bg, bg);

        // border
        let border = 200.0 / 255.0;
        if (localX < 1.0 || localX >= histWidth - 1.0 || localY < 1.0 || localY >= histHeight - 1.0) {
            result = vec3<f32>(border, border, border);
        } else {
            // bar
            let barAreaX = localX - 10.0;
            let barAreaY = localY - 10.0;
            let barAreaWidth = histWidth - 20.0;
            let barAreaHeight = histHeight - 20.0;

            if (barAreaX >= 0.0 && barAreaX < barAreaWidth && barAreaY >= 0.0 && barAreaY < barAreaHeight) {
                let barWidth = histWidth / 64.0;
                var binIndex = i32(barAreaX / barWidth);
                binIndex = clamp(binIndex, 0, 63);

                let maxCount = uniforms.velocityHistogramMaxCount;

                if (maxCount > 0) {
                    let vecIndex = binIndex / 4;
                    let component = binIndex % 4;
                    let vec = uniforms.velocityHistogramBins[vecIndex];
                    let binCount = getBinCount(vec, component);
                    let barHeight = (f32(binCount) / f32(maxCount)) * barAreaHeight;
                    let barBottom = barAreaHeight - barHeight;

                    // within bar
                    if (barAreaY >= barBottom && barAreaY < barAreaHeight) {
                        let normalized = f32(binIndex) / 64.0;
                        result = mapValueToVelocityColor(normalized, 0.0, 1.0);
                    }
                }
            }
        }

        return vec4<f32>(result, 1.0);
    }

    return vec4<f32>(0.0, 0.0, 0.0, 0.0);
}

@fragment
fn fs_main(@builtin(position) fragCoord: vec4<f32>) -> @location(0) vec4<f32> {
    let pixelCoord = fragCoord.xy;

    // histograms first (on top)
    let histColor = drawHistograms(pixelCoord);
    if (histColor.a > 0.0) {
        return histColor;
    }

    var finalColor: vec3<f32> = vec3<f32>(0.0);

    // pixel to world coords
    let worldCoord = vec2<f32>(
        pixelCoord.x / uniforms.windowWidth * uniforms.simWidth,
        (uniforms.windowHeight - pixelCoord.y) / uniforms.windowHeight * uniforms.simHeight
    );

    // convert world coords to integer grid indices
    let simCoord = worldCoord / uniforms.cellSize;
    let texCoord = vec2<i32>(i32(simCoord.x), i32(simCoord.y));

    // guard against sampling outside the simulation domain
    if (texCoord.x < 0 || texCoord.x >= uniforms.gridX ||
        texCoord.y < 0 || texCoord.y >= uniforms.gridY) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }

    // load simulation data
    let pressure = textureLoad(pressureTexture, texCoord, 0).r;
    let density = textureLoad(densityTexture, texCoord, 0).r;
    let velocity = textureLoad(velocityTexture, texCoord, 0).rg;
    let solid = textureLoad(solidTexture, texCoord, 0).r;
    let ink = textureLoad(inkTexture, texCoord, 0);

    // draw based on target
    if (solid > 0.5) {
        // fluid cell
        if (uniforms.drawTarget == 0) { // pressure
            finalColor = mapValueToColor(pressure, uniforms.pressureMin, uniforms.pressureMax);
        } else if (uniforms.drawTarget == 1) { // density
            // draw smoke/density
            finalColor = mapValueToGreyscale(density, 0.0, 1.0);
        } else if (uniforms.drawTarget == 2) { // both
            // draw pretty pressure + smoke
            finalColor = mapValueToColor(pressure, uniforms.pressureMin, uniforms.pressureMax);
            finalColor = finalColor - density * vec3<f32>(1.0, 1.0, 1.0);
            finalColor = max(finalColor, vec3<f32>(0.0, 0.0, 0.0));
        } else if (uniforms.drawTarget == 3) { // ink
            finalColor = ink.rgb;
        }
    } else {
        // boundaries in grey
        finalColor = vec3<f32>(0.47);
    }

    // draw velocity field
    if (uniforms.drawVelocities != 0) {
        var velColor = drawVelocityField(worldCoord, texCoord.x, texCoord.y);
        // blend velocity lines
        if (velColor.a > 0.0) {
            finalColor = velColor.rgb * velColor.a + finalColor * (1.0 - velColor.a);
        }
    }

    return vec4<f32>(finalColor, 1.0);
}