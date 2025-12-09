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
    velocityHistogramMin: f32,
    velocityHistogramMax: f32,
    densityHistogramMaxCount: i32,
    velocityHistogramMaxCount: i32,
    densityHistogramBins: array<vec4<i32>, 16>, // packed as vec4 for 16-byte alignment
    velocityHistogramBins: array<vec4<i32>, 16>, // packed as vec4 for 16-byte alignment
};

@group(0) @binding(0) var<uniform> uniforms: UniformData;
@group(0) @binding(1) var pressureSampler: sampler;
@group(0) @binding(2) var pressureTexture: texture_2d<f32>;
@group(0) @binding(3) var densityTexture: texture_2d<f32>;
@group(0) @binding(4) var velocityTexture: texture_2d<f32>;
@group(0) @binding(5) var solidTexture: texture_2d<f32>;
@group(0) @binding(6) var redInkTexture: texture_2d<f32>;
@group(0) @binding(7) var greenInkTexture: texture_2d<f32>;
@group(0) @binding(8) var blueInkTexture: texture_2d<f32>;

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

fn mapInkToColor(r: f32, g: f32, b: f32) -> vec3<f32> {
    var r_clamped = clamp(r, 0.0, 1.0);
    var g_clamped = clamp(g, 0.0, 1.0);
    var b_clamped = clamp(b, 0.0, 1.0);
    
    return vec3<f32>(r_clamped, g_clamped, b_clamped);
}

fn worldToScreen(worldPos: vec2<f32>) -> vec2<f32> {
    var screenPos = worldPos;
    screenPos.y = uniforms.simHeight - worldPos.y;
    screenPos = screenPos / vec2<f32>(uniforms.simWidth, uniforms.simHeight);
    screenPos = screenPos * 2.0 - 1.0;
    return screenPos;
}

fn sampleFluidField(coord: vec2<f32>) -> vec4<f32> {
    var gridCoord = coord / uniforms.cellSize;

    // integer grid coordinates
    var gridX = i32(gridCoord.x);
    var gridY = i32(gridCoord.y);

    // bounds
    if (gridX < 0 || gridX >= uniforms.gridX ||
        gridY < 0 || gridY >= uniforms.gridY) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }

    var texX = gridX; // col index
    var texY = gridY; // row index

    // load simulation data
    var pressure = textureLoad(pressureTexture, vec2<i32>(texX, texY), 0);
    var density = textureLoad(densityTexture, vec2<i32>(texX, texY), 0);
    var solid = textureLoad(solidTexture, vec2<i32>(texX, texY), 0);
    var redInk = textureLoad(redInkTexture, vec2<i32>(texX, texY), 0);
    var greenInk = textureLoad(greenInkTexture, vec2<i32>(texX, texY), 0);
    var blueInk = textureLoad(blueInkTexture, vec2<i32>(texX, texY), 0);

    var color = vec3<f32>(0.0, 0.0, 0.0);

    if (solid.r > 0.5) {
        // fluid cell
        if (uniforms.drawTarget == 0) {
            // draw pressure
            color = mapValueToColor(pressure.r, uniforms.pressureMin, uniforms.pressureMax);
        } else if (uniforms.drawTarget == 1) {
            // draw smoke/density
            color = mapValueToGreyscale(density.r, 0.0, 1.0);
        } else if (uniforms.drawTarget == 3) {
            // draw ink diffusion
            color = mapInkToColor(redInk.r, greenInk.r, blueInk.r);
        } else {
            // draw pretty pressure + smoke
            color = mapValueToColor(pressure.r, uniforms.pressureMin, uniforms.pressureMax);
            color = color - density.r * vec3<f32>(1.0, 1.0, 1.0);
            color = max(color, vec3<f32>(0.0, 0.0, 0.0));
        }
    } else {
        // boundaries in grey
        color = vec3<f32>(0.49, 0.49, 0.49);
    }

    return vec4<f32>(color, 1.0);
}

fn drawVelocityField(coord: vec2<f32>) -> vec4<f32> {
    var gridCoord = coord / uniforms.cellSize;

    // integer grid coordinates
    var gridX = i32(gridCoord.x);
    var gridY = i32(gridCoord.y);

    // bounds
    if (gridX < 0 || gridX >= uniforms.gridX ||
        gridY < 0 || gridY >= uniforms.gridY) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    var texX = gridX; // col index
    var texY = gridY; // row index

    // load simulation data
    var solid = textureLoad(solidTexture, vec2<i32>(texX, texY), 0);
    var velocity = textureLoad(velocityTexture, vec2<i32>(texX, texY), 0);

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

    return vec4<f32>(color, 0.7);
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

fn drawHistograms(pixelCoord: vec2<f32>) -> vec4<f32> {
    if (uniforms.disableHistograms != 0) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    const histWidth = 300.0;
    const histHeight = 150.0;
    
    // density histogram
    var dhistX = 10.0;
    var dhistY = 10.0;
    // velocity histogram
    var vhistX = 320.0;
    var vhistY = 10.0;
    
    // draw density histogram
    if (pixelCoord.x >= dhistX && pixelCoord.x < dhistX + histWidth &&
        pixelCoord.y >= dhistY && pixelCoord.y < dhistY + histHeight) {
        
        var localX = pixelCoord.x - dhistX;
        var localY = pixelCoord.y - dhistY;
        
        // background
        var bg = 40.0 / 255.0;
        var result = vec3<f32>(bg, bg, bg);
        
        // border
        var border = 200.0 / 255.0;
        if (localX < 1.0 || localX >= histWidth - 1.0 || localY < 1.0 || localY >= histHeight - 1.0) {
            result = vec3<f32>(border, border, border);
        } else {
            // bar
            var barAreaX = localX - 10.0;
            var barAreaY = localY - 10.0;
            var barAreaWidth = histWidth - 20.0;
            var barAreaHeight = histHeight - 20.0;
            
            if (barAreaX >= 0.0 && barAreaX < barAreaWidth && barAreaY >= 0.0 && barAreaY < barAreaHeight) {
                var barWidth = histWidth / 64.0;
                var binIndex = i32(barAreaX / barWidth);
                binIndex = clamp(binIndex, 0, 63);
                
                var maxCount = uniforms.densityHistogramMaxCount;
                
                if (maxCount > 0) {
                    var vecIndex = binIndex / 4;
                    var component = binIndex % 4;
                    var vec = uniforms.densityHistogramBins[vecIndex];
                    var binCount = 0;
                    if (component == 0) {
                        binCount = vec.x;
                    } else if (component == 1) {
                        binCount = vec.y;
                    } else if (component == 2) {
                        binCount = vec.z;
                    } else {
                        binCount = vec.w;
                    }
                    var barHeight = (f32(binCount) / f32(maxCount)) * barAreaHeight;
                    var barBottom = barAreaHeight - barHeight;
                    
                    // within bar
                    if (barAreaY >= barBottom && barAreaY < barAreaHeight) {
                        var normalized = f32(binIndex) / 64.0;
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
        
        var localX = pixelCoord.x - vhistX;
        var localY = pixelCoord.y - vhistY;
        
        // background
        var bg = 40.0 / 255.0;
        var result = vec3<f32>(bg, bg, bg);
        
        // border
        var border = 200.0 / 255.0;
        if (localX < 1.0 || localX >= histWidth - 1.0 || localY < 1.0 || localY >= histHeight - 1.0) {
            result = vec3<f32>(border, border, border);
        } else {
            // bar
            var barAreaX = localX - 10.0;
            var barAreaY = localY - 10.0;
            var barAreaWidth = histWidth - 20.0;
            var barAreaHeight = histHeight - 20.0;
            
            if (barAreaX >= 0.0 && barAreaX < barAreaWidth && barAreaY >= 0.0 && barAreaY < barAreaHeight) {
                var barWidth = histWidth / 64.0;
                var binIndex = i32(barAreaX / barWidth);
                binIndex = clamp(binIndex, 0, 63);
                
                var maxCount = uniforms.velocityHistogramMaxCount;
                
                if (maxCount > 0) {
                    var vecIndex = binIndex / 4;
                    var component = binIndex % 4;
                    var vec = uniforms.velocityHistogramBins[vecIndex];
                    var binCount = 0;
                    if (component == 0) {
                        binCount = vec.x;
                    } else if (component == 1) {
                        binCount = vec.y;
                    } else if (component == 2) {
                        binCount = vec.z;
                    } else {
                        binCount = vec.w;
                    }
                    var barHeight = (f32(binCount) / f32(maxCount)) * barAreaHeight;
                    var barBottom = barAreaHeight - barHeight;
                    
                    // within bar
                    if (barAreaY >= barBottom && barAreaY < barAreaHeight) {
                        var normalized = f32(binIndex) / 64.0;
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
    var pixelCoord = fragCoord.xy;

    // histograms first (on top)
    var histColor = drawHistograms(pixelCoord);
    if (histColor.a > 0.0) {
        return histColor;
    }

    // pixel to world coords
    var worldCoord = vec2<f32>(
        pixelCoord.x / uniforms.windowWidth * uniforms.simWidth,
        (uniforms.windowHeight - pixelCoord.y) / uniforms.windowHeight * uniforms.simHeight
    );

    var color = sampleFluidField(worldCoord);

    if (uniforms.drawVelocities != 0) {
        var velColor = drawVelocityField(worldCoord);
        // blend velocity lines
        if (velColor.a > 0.0) {
            return vec4<f32>(
                velColor.rgb * velColor.a + color.rgb * (1.0 - velColor.a),
                1.0
            );
        }
    }

    return color;
}