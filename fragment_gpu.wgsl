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

    // Viewport configuration
    viewportCount: i32,
    viewportX: array<i32, 4>,
    viewportY: array<i32, 4>,
    viewportWidth: array<i32, 4>,
    viewportHeight: array<i32, 4>,
    viewportRenderTarget: array<i32, 4>,
    viewportRenderVelocity: array<i32, 4>,
    pad1: array<i32, 3>,

    // Histogram configuration
    densityHistogramEnabled: i32,
    densityHistogramX: i32,
    densityHistogramY: i32,
    densityHistogramWidth: i32,
    densityHistogramHeight: i32,
    velocityHistogramEnabled: i32,
    velocityHistogramX: i32,
    velocityHistogramY: i32,
    velocityHistogramWidth: i32,
    velocityHistogramHeight: i32,

    // Histogram data
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

// TODO WIP design full interface with control panel, several views, live view changing (e.g. pretty/smoke/density), config refreshing, etc.
// - config editing
// - more visualization options (consider combined overlay of fluid over camera canvas, etc.)

// TODO overall hand stability just not giving the UX I want, want smoother displacement of fluid
// - can we use velocity to "eject" existing fluid out of the way? can we manually override and "teleport" fluid out of the way? investigate momentum transfer?
// - !! can we just nullify all keypoint movements below a certain threshold? [NEXT]

// TODO new two handed control system (with left-handed toggle)
// - control panel

// TODO gravity basically broken

// TODO big cleanup: test, unify, and document codebase, particularly build steps + config (& generally simplify flow of data/modularize)
// TODO unify cpu and gpu stuff
// TODO python script for modifying simParams struct uniformly

/*
// TODO examine differences in pressure/velocity histograms between GPU/CPU
- see plotting script
- note: it seems like we can get better behavior by cranking Jacobi iterations up (1000)
// TODO LATER swap to different pressure solver

// TODO up/right wind tunnel behaves differently from down/left
likely because of the forward texture accesses (e.g. asymmetric +1) somewhere
or it could be something else...
- there are still some subtle bugs with gpu sim (bot left corner, circle detection)
- and general poor behavior at high magnitudes (e.g. of velocity)--are we correctly checking fluids at boundaries
  and is there any undefined behavior during advection?

// TODO LATER integrate gravity with gyroscope on the phone

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

fn getViewportForPixel(pixelCoord: vec2<f32>) -> i32 {
    if (uniforms.viewportCount == 0) {
        return -1; // No viewports defined, use default rendering
    }

    for (var i = 0; i < uniforms.viewportCount; i++) {
        let vx = f32(uniforms.viewportX[i]);
        let vy = f32(uniforms.viewportY[i]);
        let vw = f32(uniforms.viewportWidth[i]);
        let vh = f32(uniforms.viewportHeight[i]);

        if (pixelCoord.x >= vx && pixelCoord.x < vx + vw &&
            pixelCoord.y >= vy && pixelCoord.y < vy + vh) {
            return i;
        }
    }
    return -1; // Not in any viewport
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

    // Draw density histogram (configurable position)
    if (uniforms.densityHistogramEnabled != 0) {
        let dhistX = f32(uniforms.densityHistogramX);
        let dhistY = f32(uniforms.densityHistogramY);
        let histWidth = f32(uniforms.densityHistogramWidth);
        let histHeight = f32(uniforms.densityHistogramHeight);

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
                // bar area
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
    }

    // Draw velocity histogram (configurable position)
    if (uniforms.velocityHistogramEnabled != 0) {
        let vhistX = f32(uniforms.velocityHistogramX);
        let vhistY = f32(uniforms.velocityHistogramY);
        let histWidth = f32(uniforms.velocityHistogramWidth);
        let histHeight = f32(uniforms.velocityHistogramHeight);

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
                // bar area
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
    }

    return vec4<f32>(0.0, 0.0, 0.0, 0.0);
}

@fragment
fn fs_main(@builtin(position) fragCoord: vec4<f32>) -> @location(0) vec4<f32> {
    let pixelCoord = fragCoord.xy;

    // histograms first (on top of everything)
    let histColor = drawHistograms(pixelCoord);
    if (histColor.a > 0.0) {
        return histColor;
    }

    // Check which viewport we're in
    let viewportIndex = getViewportForPixel(pixelCoord);

    var finalColor: vec3<f32> = vec3<f32>(0.0);

    if (viewportIndex >= 0) {
        // Render to viewport
        let vx = f32(uniforms.viewportX[viewportIndex]);
        let vy = f32(uniforms.viewportY[viewportIndex]);
        let vw = f32(uniforms.viewportWidth[viewportIndex]);
        let vh = f32(uniforms.viewportHeight[viewportIndex]);

        // Transform pixel to simulation coordinates relative to viewport
        let normX = (pixelCoord.x - vx) / vw;
        let normY = (pixelCoord.y - vy) / vh;
        let worldCoord = vec2<f32>(
            normX * uniforms.simWidth,
            (1.0 - normY) * uniforms.simHeight  // flip Y for WebGL convention
        );

        // Convert to grid coordinates
        let simCoord = worldCoord / uniforms.cellSize;
        let texCoord = vec2<i32>(i32(simCoord.x), i32(simCoord.y));

        // Guard against sampling outside the simulation domain
        if (texCoord.x < 0 || texCoord.x >= uniforms.gridX ||
            texCoord.y < 0 || texCoord.y >= uniforms.gridY) {
            return vec4<f32>(0.0, 0.0, 0.0, 1.0);
        }

        // Use viewport's render target
        let viewportRenderTarget = uniforms.viewportRenderTarget[viewportIndex];

        // Load and render simulation data
        let pressure = textureLoad(pressureTexture, texCoord, 0).r;
        let density = textureLoad(densityTexture, texCoord, 0).r;
        let velocity = textureLoad(velocityTexture, texCoord, 0).rg;
        let solid = textureLoad(solidTexture, texCoord, 0).r;
        let ink = textureLoad(inkTexture, texCoord, 0);

        if (solid > 0.5) {
            if (viewportRenderTarget == 0) {
                finalColor = mapValueToColor(pressure, uniforms.pressureMin, uniforms.pressureMax);
            } else if (viewportRenderTarget == 1) {
                finalColor = mapValueToGreyscale(density, 0.0, 1.0);
            } else if (viewportRenderTarget == 2) {
                finalColor = mapValueToColor(pressure, uniforms.pressureMin, uniforms.pressureMax);
                finalColor = finalColor - density * vec3<f32>(1.0, 1.0, 1.0);
                finalColor = max(finalColor, vec3<f32>(0.0, 0.0, 0.0));
            } else if (viewportRenderTarget == 3) {
                finalColor = ink.rgb;
            }
        } else {
            finalColor = vec3<f32>(0.47);
        }

        // Draw velocity field if enabled for this viewport
        if (uniforms.viewportRenderVelocity[viewportIndex] != 0) {
            var velColor = drawVelocityField(worldCoord, texCoord.x, texCoord.y);
            if (velColor.a > 0.0) {
                finalColor = velColor.rgb * velColor.a + finalColor * (1.0 - velColor.a);
            }
        }
    } else {
        // background (matches charliemax.dev --offblack)
        finalColor = vec3<f32>(5.0 / 255.0);
    }

    return vec4<f32>(finalColor, 1.0);
}