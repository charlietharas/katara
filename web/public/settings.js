import {
    Palette,
    ControlPalette,
    HandPalette,
    layoutTint,
} from './palette.js';

function isPlainObject(v) {
    return !!v && typeof v === 'object' && !Array.isArray(v);
}

function deepMerge(target, source) {
    if (!isPlainObject(source)) return target;
    Object.keys(source).forEach((key) => {
        const srcVal = source[key];
        const dstVal = target[key];
        if (isPlainObject(srcVal) && isPlainObject(dstVal)) {
            deepMerge(dstVal, srcVal);
        } else if (isPlainObject(srcVal)) {
            target[key] = deepMerge({}, srcVal);
        } else {
            target[key] = srcVal;
        }
    });
    return target;
}

function getNestedValue(obj, path) {
    let current = obj;
    for (const key of path.split('.')) {
        if (current && key in current) {
            current = current[key];
        } else {
            return undefined;
        }
    }
    return current;
}

function setNestedValue(obj, path, value) {
    const keys = path.split('.');
    let current = obj;
    for (let i = 0; i < keys.length - 1; i++) {
        if (!(keys[i] in current)) current[keys[i]] = {};
        current = current[keys[i]];
    }
    current[keys[keys.length - 1]] = value;
}

function isGpuSimulatorMode(config) {
    return config?.pipeline === 'device';
}

const RESOLUTION_RANGES = {
    device: { min: 100, max: 800, step: 50 },
    hybrid: { min: 100, max: 400, step: 20 },
};

const ITERATION_RANGES = {
    device: { min: 100, max: 1000, step: 50 },
    hybrid: { min: 20, max: 200, step: 10 },
};

const PIPELINE_DEFAULTS = {
    device: { resolution: 400, iterations: 100 },
    hybrid: { resolution: 100, iterations: 20 },
};

function pipelineMode(pipeline) {
    return pipeline === 'hybrid' ? 'hybrid' : 'device';
}

function resolutionRangeForPipeline(pipeline) {
    return RESOLUTION_RANGES[pipelineMode(pipeline)];
}

function iterationRangeForPipeline(pipeline) {
    return ITERATION_RANGES[pipelineMode(pipeline)];
}

function defaultResolutionForPipeline(pipeline) {
    return PIPELINE_DEFAULTS[pipelineMode(pipeline)].resolution;
}

function defaultIterationsForPipeline(pipeline) {
    return PIPELINE_DEFAULTS[pipelineMode(pipeline)].iterations;
}

function clampResolutionForPipeline(value, pipeline) {
    const { min, max } = resolutionRangeForPipeline(pipeline);
    return Math.max(min, Math.min(max, value));
}

function clampIterationsForPipeline(value, pipeline) {
    const { min, max } = iterationRangeForPipeline(pipeline);
    return Math.max(min, Math.min(max, value));
}

function applySliderRange(control, range, clampedValue) {
    control.setRange(range.min, range.max, range.step);
    control.currentValue = clampedValue;
    const input = control.element?.querySelector('input[type="range"]');
    const display = control.element?.querySelector('.value-display');
    if (input) input.value = clampedValue;
    if (display) display.textContent = formatSliderValue(clampedValue, range.step);
}

function measureHiddenElement(el, dimension) {
    if (!el) return 0;
    const prop = dimension === 'width' ? 'offsetWidth' : 'offsetHeight';
    if (el[prop] > 0) return el[prop];
    const prevDisplay = el.style.display;
    const prevVisibility = el.style.visibility;
    el.style.visibility = 'hidden';
    el.style.display = '';
    const size = el[prop];
    el.style.display = prevDisplay;
    el.style.visibility = prevVisibility;
    return size;
}

function measureElementWidth(el) {
    return measureHiddenElement(el, 'width');
}

function measureElementHeight(el) {
    return measureHiddenElement(el, 'height');
}

function getSimViewportSize() {
    const viewport = document.querySelector('.sim-viewport');
    if (!viewport) {
        throw new Error('Sim viewport not found');
    }
    return {
        width: viewport.clientWidth,
        height: viewport.clientHeight,
    };
}

function getSimCanvasAspectRatio() {
    const app = window.kataraApp;
    let domainAspect = null;
    if (app?.module?._getSimDomainWidth && app?.module?._getSimDomainHeight) {
        const domainWidth = app.module._getSimDomainWidth();
        const domainHeight = app.module._getSimDomainHeight();
        if (domainWidth > 0 && domainHeight > 0) {
            domainAspect = domainWidth / domainHeight;
        }
    }

    const config = window.kataraConfig ?? app?.readConfig?.();
    const useInkAspect = !!config?.imagePath;
    const inkAspect = useInkAspect ? app?.inkAspectRatio : 0;
    // After image upload, inkAspectRatio updates immediately but sim domain may stay stale until reload.
    if (inkAspect > 0 && domainAspect !== null) {
        const drift = Math.abs(inkAspect - domainAspect) / Math.max(domainAspect, 0.001);
        if (drift > 0.02) return inkAspect;
        return domainAspect;
    }
    if (inkAspect > 0) return inkAspect;
    if (domainAspect !== null) return domainAspect;
    return 1.0;
}

function bindSliderRow(row, { onChange, format = String }) {
    const input = row.querySelector('input');
    const display = row.querySelector('.value-display');
    input.addEventListener('input', () => {
        const value = parseFloat(input.value);
        display.textContent = format(value);
        onChange(value);
    });
    return { input, display };
}

function formatSliderValue(value, step) {
    const stepText = String(step);
    const dot = stepText.indexOf('.');
    const decimals = dot === -1 ? 0 : stepText.length - dot - 1;
    if (decimals === 0) return String(Math.round(value));
    return value.toFixed(decimals);
}

function preventCheckboxEnterDefault(checkbox) {
    checkbox.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault();
        }
    });
}

function setupResizableCanvas(canvas, onResize) {
    const resize = () => {
        const rect = canvas.parentElement.getBoundingClientRect();
        canvas.width = rect.width;
        canvas.height = rect.height;
        onResize();
    };
    resize();
    new ResizeObserver(resize).observe(canvas.parentElement);
    return resize;
}

function getCanvasEventPos(canvas, e) {
    const canvasRect = canvas.getBoundingClientRect();
    const scaleX = canvas.width / canvasRect.width;
    const scaleY = canvas.height / canvasRect.height;
    return {
        x: (e.clientX - canvasRect.left) * scaleX,
        y: (e.clientY - canvasRect.top) * scaleY,
    };
}

function isNearPoint(pos, tip, threshold = 15) {
    const dx = pos.x - tip.x;
    const dy = pos.y - tip.y;
    return dx * dx + dy * dy < threshold * threshold;
}

const CONTROL_ARROW_HIT_THRESHOLD = 12;
const CONTROL_ARROW_HOVER_THRESHOLD = 14;
const CONTROL_ARROW_HOVER_EXPAND = 1.4;
const CONTROL_ARROW_HOVER_LERP = 10;
const CONTROL_ARROW_GLOW_SNAP = 0.02;

function advanceControlCanvasGlow(values, targets, keys, dt) {
    let animating = false;
    for (const key of keys) {
        const current = values[key] ?? 0;
        const target = targets[key] ?? 0;
        let next = current + (target - current) * Math.min(1, CONTROL_ARROW_HOVER_LERP * dt);
        if (target === 0 && next < CONTROL_ARROW_GLOW_SNAP) {
            next = 0;
        } else if (Math.abs(next - target) > 0.008) {
            animating = true;
        } else {
            next = target;
        }
        if (next === 0) {
            delete values[key];
        } else {
            values[key] = next;
        }
        if (next !== target) animating = true;
    }
    return animating;
}

function setupControlCanvasArrowInteraction(canvas, { getTips, getArrowKeys, onRedraw }) {
    const glow = {};
    const targets = {};
    let pinnedKey = null;
    let rafId = null;
    let lastTime = 0;

    const getGlow = (key) => glow[key] ?? 0;
    const getScale = (key) => 1 + getGlow(key) * (CONTROL_ARROW_HOVER_EXPAND - 1);

    const distanceSq = (pos, tip) => {
        const dx = pos.x - tip.x;
        const dy = pos.y - tip.y;
        return dx * dx + dy * dy;
    };

    const getActiveArrowKey = (pos, { forHit = false } = {}) => {
        if (pinnedKey) return pinnedKey;
        if (!pos) return null;

        const tips = getTips();
        let closestKey = null;
        let closestDistSq = Infinity;
        const threshold = forHit ? CONTROL_ARROW_HIT_THRESHOLD : CONTROL_ARROW_HOVER_THRESHOLD;
        const maxDistSq = threshold * threshold;

        for (const key of getArrowKeys()) {
            const tip = tips[key];
            if (!tip) continue;
            const distSq = distanceSq(pos, tip);
            if (distSq <= maxDistSq && distSq < closestDistSq) {
                closestDistSq = distSq;
                closestKey = key;
            }
        }

        return closestKey;
    };

    const syncTargets = (pos) => {
        const activeKey = getActiveArrowKey(pos);
        for (const key of getArrowKeys()) {
            targets[key] = key === activeKey ? 1 : 0;
        }
    };

    const isNearArrow = (pos, key) => getActiveArrowKey(pos, { forHit: true }) === key;

    const isInteractiveAt = (pos) => getActiveArrowKey(pos, { forHit: true }) !== null;

    const tick = (now) => {
        const dt = Math.min(0.05, (now - lastTime) / 1000);
        lastTime = now;
        const animating = advanceControlCanvasGlow(glow, targets, getArrowKeys(), dt);

        onRedraw();
        rafId = animating ? requestAnimationFrame(tick) : null;
    };

    const scheduleTick = () => {
        if (rafId !== null) return;
        lastTime = performance.now();
        rafId = requestAnimationFrame(tick);
    };

    const updatePointer = (pos) => {
        syncTargets(pos);
        scheduleTick();
    };

    const updateCursor = (e) => {
        if (!e) {
            canvas.style.cursor = 'default';
            if (!pinnedKey) {
                updatePointer(null);
            }
            return;
        }
        const pos = getCanvasEventPos(canvas, e);
        canvas.style.cursor = isInteractiveAt(pos) || pinnedKey ? 'crosshair' : 'default';
        updatePointer(pos);
    };

    canvas.addEventListener('mousemove', updateCursor);
    canvas.addEventListener('mouseleave', () => {
        if (pinnedKey) {
            canvas.style.cursor = 'crosshair';
            updatePointer(null);
            return;
        }
        canvas.style.cursor = 'default';
        updatePointer(null);
    });

    return {
        updateCursor,
        getGlow,
        getScale,
        isNearArrow,
        refreshGlow(pos = null) {
            updatePointer(pos);
        },
        setPinned(key, active) {
            pinnedKey = active ? key : null;
            updatePointer(null);
        },
    };
}

function setupControlCanvasGlowInteraction(canvas, { getActiveKey, getKeys, onRedraw }) {
    const glow = {};
    const targets = {};
    let rafId = null;
    let lastTime = 0;

    const getGlow = (key) => glow[key] ?? 0;

    const syncTargets = (activeKey) => {
        for (const key of getKeys()) {
            targets[key] = key === activeKey ? 1 : 0;
        }
    };

    const tick = (now) => {
        const dt = Math.min(0.05, (now - lastTime) / 1000);
        lastTime = now;
        const animating = advanceControlCanvasGlow(glow, targets, getKeys(), dt);

        onRedraw();
        rafId = animating ? requestAnimationFrame(tick) : null;
    };

    const scheduleTick = () => {
        if (rafId !== null) return;
        lastTime = performance.now();
        rafId = requestAnimationFrame(tick);
    };

    const updatePointer = (pos) => {
        syncTargets(getActiveKey(pos));
        scheduleTick();
    };

    canvas.addEventListener('mousemove', (e) => {
        updatePointer(getCanvasEventPos(canvas, e));
    });
    canvas.addEventListener('mouseleave', () => updatePointer(null));

    return {
        getGlow,
        refreshGlow(pos = null) {
            updatePointer(pos);
        },
    };
}

function updateControlScaleIndicator(element, displayScale) {
    if (!element) return;
    element.classList.toggle(
        'control-display-clamped',
        !WorldScaleService.isPixelAccurateScale(displayScale)
    );
}

function applyControlCanvasGlow(ctx, glowT) {
    const t = Math.max(0, Math.min(1, glowT));
    if (t <= 0) return;
    ctx.shadowColor = Palette.whiteBright;
    ctx.shadowBlur = t * 20;
}

function buildEnvironmentArrowGeometry(from, tip, baseWidth, headWidth, headLength) {
    const dx = tip.x - from.x;
    const dy = tip.y - from.y;
    const length = Math.sqrt(dx * dx + dy * dy);
    if (length < 0.001) {
        return { body: [], head: [], full: [] };
    }
    const ux = dx / length;
    const uy = dy / length;
    const px = -uy;
    const py = ux;
    const baseHalf = baseWidth * 0.5;
    const headHalf = headWidth * 0.5;
    const headStart = Math.max(0, length - headLength);
    const flareStart = Math.max(0, headStart - headLength * 0.45);
    const flareX = from.x + ux * flareStart;
    const flareY = from.y + uy * flareStart;
    const neckX = from.x + ux * headStart;
    const neckY = from.y + uy * headStart;

    const baseL = { x: from.x + px * baseHalf, y: from.y + py * baseHalf };
    const baseR = { x: from.x - px * baseHalf, y: from.y - py * baseHalf };
    const flareL = { x: flareX + px * baseHalf, y: flareY + py * baseHalf };
    const flareR = { x: flareX - px * baseHalf, y: flareY - py * baseHalf };
    const neckL = { x: neckX + px * headHalf, y: neckY + py * headHalf };
    const neckR = { x: neckX - px * headHalf, y: neckY - py * headHalf };
    const tipPt = { x: tip.x, y: tip.y };

    return {
        body: [baseL, flareL, neckL, neckR, flareR, baseR],
        head: [neckL, tipPt, neckR],
        full: [baseL, flareL, neckL, tipPt, neckR, flareR, baseR],
    };
}

function isPointInPolygon(point, polygon) {
    if (polygon.length < 3) return false;
    let inside = false;
    for (let i = 0, j = polygon.length - 1; i < polygon.length; j = i++) {
        if (pointToSegmentDistance(point, polygon[j], polygon[i]) <= 1.0) {
            return true;
        }
        const xi = polygon[i].x;
        const yi = polygon[i].y;
        const xj = polygon[j].x;
        const yj = polygon[j].y;
        const intersect = ((yi > point.y) !== (yj > point.y)) &&
            (point.x < ((xj - xi) * (point.y - yi)) / (yj - yi) + xi);
        if (intersect) inside = !inside;
    }
    return inside;
}

function addControlArrowHeadPath(ctx, tip, headDir, headSize) {
    if (headDir === 'up') {
        ctx.moveTo(tip.x, tip.y);
        ctx.lineTo(tip.x - headSize / 2, tip.y + headSize);
        ctx.lineTo(tip.x + headSize / 2, tip.y + headSize);
    } else if (headDir === 'left') {
        ctx.moveTo(tip.x, tip.y);
        ctx.lineTo(tip.x - headSize, tip.y - headSize / 2);
        ctx.lineTo(tip.x - headSize, tip.y + headSize / 2);
    } else {
        const halfWidth = 6;
        const dirX = Math.SQRT1_2;
        const dirY = -Math.SQRT1_2;
        const baseX = tip.x - headSize * dirX;
        const baseY = tip.y - headSize * dirY;
        ctx.moveTo(tip.x, tip.y);
        ctx.lineTo(baseX + halfWidth * dirY, baseY - halfWidth * dirX);
        ctx.lineTo(baseX - halfWidth * dirY, baseY + halfWidth * dirX);
    }
}

function drawControlArrow(ctx, from, tip, color, headDir, glowT = 0) {
    const headSize = 8;
    const tipScale = 1 + glowT * (CONTROL_ARROW_HOVER_EXPAND - 1);

    ctx.save();
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(from.x, from.y);
    ctx.lineTo(tip.x, tip.y);
    ctx.stroke();
    ctx.restore();

    ctx.save();
    if (tipScale !== 1) {
        ctx.translate(tip.x, tip.y);
        ctx.scale(tipScale, tipScale);
        ctx.translate(-tip.x, -tip.y);
    }
    applyControlCanvasGlow(ctx, glowT);
    ctx.fillStyle = color;
    ctx.beginPath();
    addControlArrowHeadPath(ctx, tip, headDir, headSize);
    ctx.closePath();
    ctx.fill();
    ctx.restore();
}

function pointToSegmentDistance(point, a, b) {
    const abX = b.x - a.x;
    const abY = b.y - a.y;
    const apX = point.x - a.x;
    const apY = point.y - a.y;
    const abLenSq = abX * abX + abY * abY;
    if (abLenSq === 0) {
        const dx = point.x - a.x;
        const dy = point.y - a.y;
        return Math.sqrt(dx * dx + dy * dy);
    }
    const t = Math.max(0, Math.min(1, (apX * abX + apY * abY) / abLenSq));
    const closestX = a.x + t * abX;
    const closestY = a.y + t * abY;
    const dx = point.x - closestX;
    const dy = point.y - closestY;
    return Math.sqrt(dx * dx + dy * dy);
}

function fitCenteredRect(w, h, aspect) {
    let contentW = w;
    let contentH = w / aspect;
    if (contentH > h) {
        contentH = h;
        contentW = h * aspect;
    }
    return {
        x: (w - contentW) / 2,
        y: (h - contentH) / 2,
        width: contentW,
        height: contentH,
    };
}

const HAND_MODES = ['full', 'joints', 'claw', 'pointer', 'pointer-tip', 'none'];

const HAND_MODE_LABELS = {
    full: 'Full',
    joints: 'Joints',
    pointer: 'Pointer',
    'pointer-tip': 'Dot',
    claw: 'Claw',
    none: 'Off',
};

const HAND_COLORS = HandPalette;

const ACTIVE_LANDMARKS_BY_MODE = {
    full: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20],
    joints: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20],
    pointer: [5, 6, 7, 8],
    'pointer-tip': [8],
    claw: [0, 1, 2, 3, 4, 5, 6, 7, 8],
    none: [],
};

const HAND_CONNECTIONS = [
    [0, 1], [1, 2], [2, 3], [3, 4],
    [0, 5], [5, 6], [6, 7], [7, 8],
    [0, 9], [9, 10], [10, 11], [11, 12],
    [0, 13], [13, 14], [14, 15], [15, 16],
    [0, 17], [17, 18], [18, 19], [19, 20],
    [5, 9], [9, 13], [13, 17],
];

const HAND_LANDMARKS = [
    { x: 0.50, y: 0.90 },
    { x: 0.28, y: 0.76 },
    { x: 0.18, y: 0.60 },
    { x: 0.13, y: 0.44 },
    { x: 0.10, y: 0.28 },
    { x: 0.35, y: 0.42 },
    { x: 0.32, y: 0.26 },
    { x: 0.30, y: 0.12 },
    { x: 0.28, y: 0.00 },
    { x: 0.48, y: 0.38 },
    { x: 0.48, y: 0.21 },
    { x: 0.48, y: 0.08 },
    { x: 0.48, y: 0.00 },
    { x: 0.60, y: 0.42 },
    { x: 0.62, y: 0.27 },
    { x: 0.63, y: 0.14 },
    { x: 0.64, y: 0.03 },
    { x: 0.70, y: 0.48 },
    { x: 0.74, y: 0.36 },
    { x: 0.77, y: 0.25 },
    { x: 0.80, y: 0.15 },
];

class WorldScaleService {
    static getPixelsPerWorldUnit() {
        const viewport = document.querySelector('.sim-viewport');
        if (!viewport) return 10;

        const viewportWidth = viewport.clientWidth;
        const viewportHeight = viewport.clientHeight;
        const gridResolution = WorldScaleService.getGridResolution() || 400;
        const maxDimension = Math.max(viewportWidth, viewportHeight);
        return maxDimension / gridResolution;
    }

    // TODO jank review this
    static getGridResolution() {
        return window.kataraConfig?.simulation?.resolution || 400;
    }

    static worldToPixels(worldUnits) {
        return worldUnits * WorldScaleService.getPixelsPerWorldUnit();
    }

    static pixelsToWorld(pixels) {
        return pixels / WorldScaleService.getPixelsPerWorldUnit();
    }

    static getCanvasFitScale(halfExtent, maxExtentPx, padding = 16) {
        const available = Math.max(0, halfExtent - padding);
        if (maxExtentPx <= available || maxExtentPx <= 0) return 1;
        return available / maxExtentPx;
    }

    static isPixelAccurateScale(displayScale) {
        return displayScale >= 0.999;
    }
}

const LAYOUT_PRESET_OPTIONS = [
    { value: 'default', label: 'Dashboard' },
    { value: 'focused', label: 'Focused' },
    { value: 'viewport', label: 'Viewport' },
    { value: 'gallery_single', label: 'Gallery (single)' },
    { value: 'gallery_double', label: 'Gallery (double)' },
    { value: 'gallery_quad', label: 'Gallery (quad)' },
];

function getLayoutPresetSvgMarkup(preset) {
    const stroke = Palette.whiteFaint;
    const viewport = layoutTint(Palette.blue);
    const plot = layoutTint(Palette.green);
    const camera = layoutTint(Palette.orange);
    const grid = Palette.gridLine;

    if (preset === 'default') {
        return `
            <svg viewBox="0 0 100 70" aria-hidden="true">
                <rect x="2" y="2" width="96" height="66" rx="4" fill="none" stroke="${stroke}" stroke-width="2"/>
                <line x1="50" y1="2" x2="50" y2="68" stroke="${grid}" stroke-width="1"/>
                <line x1="2" y1="35" x2="98" y2="35" stroke="${grid}" stroke-width="1"/>
                <rect x="52" y="4" width="44" height="29" rx="2" fill="${viewport}"/>
                <rect x="52" y="37" width="44" height="29" rx="2" fill="${viewport}"/>
                <rect x="4" y="37" width="44" height="29" rx="2" fill="${viewport}"/>
                <rect x="4" y="4" width="22" height="10" rx="1.5" fill="${plot}"/>
                <rect x="26" y="4" width="22" height="10" rx="1.5" fill="${plot}"/>
                <rect x="4" y="14" width="22" height="10" rx="1.5" fill="${plot}"/>
                <rect x="4" y="24" width="22" height="10" rx="1.5" fill="${plot}"/>
                <rect x="26" y="14" width="22" height="20" rx="1.5" fill="${camera}"/>
            </svg>
        `;
    }

    if (preset === 'focused') {
        return `
            <svg viewBox="0 0 100 70" aria-hidden="true">
                <rect x="2" y="2" width="96" height="66" rx="4" fill="none" stroke="${stroke}" stroke-width="2"/>
                <line x1="50" y1="2" x2="50" y2="68" stroke="${grid}" stroke-width="1"/>
                <line x1="2" y1="35" x2="98" y2="35" stroke="${grid}" stroke-width="1"/>
                <rect x="4" y="4" width="44" height="29" rx="2" fill="${camera}"/>
                <rect x="4" y="37" width="22" height="14" rx="1.5" fill="${plot}"/>
                <rect x="26" y="37" width="22" height="14" rx="1.5" fill="${plot}"/>
                <rect x="4" y="51" width="22" height="14" rx="1.5" fill="${plot}"/>
                <rect x="26" y="51" width="22" height="14" rx="1.5" fill="${plot}"/>
                <rect x="52" y="4" width="44" height="29" rx="2" fill="${viewport}"/>
                <rect x="52" y="37" width="44" height="29" rx="2" fill="${viewport}"/>
            </svg>
        `;
    }

    if (preset === 'viewport') {
        return `
            <svg viewBox="0 0 100 70" aria-hidden="true">
                <rect x="2" y="2" width="96" height="66" rx="4" fill="none" stroke="${stroke}" stroke-width="2"/>
                <line x1="50" y1="2" x2="50" y2="68" stroke="${grid}" stroke-width="1"/>
                <line x1="2" y1="35" x2="98" y2="35" stroke="${grid}" stroke-width="1"/>
                <rect x="4" y="4" width="44" height="29" rx="2" fill="${viewport}"/>
                <rect x="52" y="4" width="44" height="29" rx="2" fill="${viewport}"/>
                <rect x="4" y="37" width="44" height="29" rx="2" fill="${viewport}"/>
                <rect x="52" y="37" width="44" height="29" rx="2" fill="${viewport}"/>
            </svg>
        `;
    }

    if (preset === 'gallery_single') {
        return `
            <svg viewBox="0 0 100 70" aria-hidden="true">
                <rect x="2" y="2" width="96" height="66" rx="4" fill="none" stroke="${stroke}" stroke-width="2"/>
                <line x1="2" y1="40" x2="98" y2="40" stroke="${grid}" stroke-width="1"/>
                <rect x="4" y="4" width="92" height="34" rx="2" fill="${viewport}"/>
                <rect x="4" y="43" width="18" height="23" rx="2" fill="${camera}"/>
                <rect x="24" y="43" width="18" height="23" rx="1.5" fill="${plot}"/>
                <rect x="42" y="43" width="18" height="23" rx="1.5" fill="${plot}"/>
                <rect x="60" y="43" width="18" height="23" rx="1.5" fill="${plot}"/>
                <rect x="78" y="43" width="18" height="23" rx="1.5" fill="${plot}"/>
            </svg>
        `;
    }

    if (preset === 'gallery_double') {
        return `
            <svg viewBox="0 0 100 70" aria-hidden="true">
                <rect x="2" y="2" width="96" height="66" rx="4" fill="none" stroke="${stroke}" stroke-width="2"/>
                <line x1="2" y1="40" x2="98" y2="40" stroke="${grid}" stroke-width="1"/>
                <line x1="50" y1="2" x2="50" y2="40" stroke="${grid}" stroke-width="1"/>
                <rect x="4" y="4" width="44" height="34" rx="2" fill="${viewport}"/>
                <rect x="52" y="4" width="44" height="34" rx="2" fill="${viewport}"/>
                <rect x="4" y="43" width="18" height="23" rx="2" fill="${camera}"/>
                <rect x="24" y="43" width="18" height="23" rx="1.5" fill="${plot}"/>
                <rect x="42" y="43" width="18" height="23" rx="1.5" fill="${plot}"/>
                <rect x="60" y="43" width="18" height="23" rx="1.5" fill="${plot}"/>
                <rect x="78" y="43" width="18" height="23" rx="1.5" fill="${plot}"/>
            </svg>
        `;
    }

    return `
        <svg viewBox="0 0 100 70" aria-hidden="true">
            <rect x="2" y="2" width="96" height="66" rx="4" fill="none" stroke="${stroke}" stroke-width="2"/>
            <line x1="2" y1="40" x2="98" y2="40" stroke="${grid}" stroke-width="1"/>
            <line x1="50" y1="2" x2="50" y2="40" stroke="${grid}" stroke-width="1"/>
            <line x1="2" y1="21" x2="98" y2="21" stroke="${grid}" stroke-width="1"/>
            <rect x="4" y="4" width="44" height="16" rx="2" fill="${viewport}"/>
            <rect x="52" y="4" width="44" height="16" rx="2" fill="${viewport}"/>
            <rect x="4" y="22" width="44" height="16" rx="2" fill="${viewport}"/>
            <rect x="52" y="22" width="44" height="16" rx="2" fill="${viewport}"/>
            <rect x="4" y="43" width="18" height="23" rx="2" fill="${camera}"/>
            <rect x="24" y="43" width="18" height="23" rx="1.5" fill="${plot}"/>
            <rect x="42" y="43" width="18" height="23" rx="1.5" fill="${plot}"/>
            <rect x="60" y="43" width="18" height="23" rx="1.5" fill="${plot}"/>
            <rect x="78" y="43" width="18" height="23" rx="1.5" fill="${plot}"/>
        </svg>
    `;
}

class ConfigControl {
    constructor(configPath, label, initialValue) {
        this.configPath = configPath;
        this.label = label;
        this.initialValue = initialValue;
        this.currentValue = initialValue;
        this.element = null;
        this.disabled = false;
        this.collectWhenDisabled = false;
    }

    setDisabled(disabled) {
        this.disabled = disabled;
        if (!this.element) return;
        this.element.classList.toggle('config-row-disabled', disabled);
        const input = this.element.querySelector('input');
        if (input) input.disabled = disabled;
    }
}

class SliderControl extends ConfigControl {
    constructor(configPath, label, initialValue, min, max, step) {
        super(configPath, label, initialValue);
        this.min = min;
        this.max = max;
        this.step = step;
        this.liveChange = null;
    }

    render() {
        const container = document.createElement('div');
        container.className = 'config-row';
        const format = (value) => formatSliderValue(value, this.step);
        container.innerHTML = `
            <label>${this.label}</label>
            <input type="range" min="${this.min}" max="${this.max}" step="${this.step}" value="${this.initialValue}">
            <span class="value-display">${format(this.initialValue)}</span>
        `;
        bindSliderRow(container, {
            onChange: (value) => {
                this.currentValue = value;
                this.liveChange?.(value);
            },
            format,
        });
        this.element = container;
        if (this.disabled) this.setDisabled(true);
        return container;
    }

    getValue() { return this.currentValue; }

    setRange(min, max, step) {
        this.min = min;
        this.max = max;
        this.step = step;
        this.currentValue = Math.max(min, Math.min(max, this.currentValue));
        if (!this.element) return;
        const input = this.element.querySelector('input[type="range"]');
        const display = this.element.querySelector('.value-display');
        if (input) {
            input.min = String(min);
            input.max = String(max);
            input.step = String(step);
            input.value = this.currentValue;
        }
        if (display) {
            display.textContent = formatSliderValue(this.currentValue, step);
        }
    }
}

class ToggleSliderControl extends SliderControl {
    constructor(configPath, label, initialValue, min, max, step) {
        super(configPath, label, initialValue, min, max, step);
        this.userEnabled = initialValue !== 0;
        this.storedValue = initialValue !== 0 ? initialValue : -1;
    }

    render() {
        const container = document.createElement('div');
        container.className = 'config-row config-row-toggle';
        const format = (value) => formatSliderValue(value, this.step);
        container.innerHTML = `
            <label>${this.label}</label>
            <input type="range" min="${this.min}" max="${this.max}" step="${this.step}" value="${this.initialValue}">
            <span class="value-display">${format(this.initialValue)}</span>
        `;
        this.input = container.querySelector('input');
        this.display = container.querySelector('.value-display');
        bindSliderRow(container, {
            onChange: (value) => {
                this.currentValue = value;
                this.storedValue = value;
                this.liveChange?.(value);
            },
            format,
        });
        container.addEventListener('click', (e) => {
            if (this.disabled) return;
            if (e.target.closest('input[type="range"]')) return;
            this.setUserEnabled(!this.userEnabled);
        });
        this.element = container;
        if (this.disabled) this.setDisabled(true);
        else this.updateUserEnabledState();
        return container;
    }

    setUserEnabled(enabled) {
        if (this.disabled) return;
        this.userEnabled = enabled;
        this.updateUserEnabledState();
    }

    updateUserEnabledState() {
        if (!this.element) return;
        const format = (value) => formatSliderValue(value, this.step);
        const enabled = this.userEnabled && !this.disabled;
        this.element.classList.toggle('config-row-disabled', !enabled);
        if (this.input) this.input.disabled = !enabled;
        if (!enabled) {
            if (this.display) this.display.textContent = format(0);
        } else if (this.input && this.display) {
            this.input.value = this.storedValue;
            this.currentValue = this.storedValue;
            this.display.textContent = format(this.storedValue);
        }
    }

    getValue() {
        return this.userEnabled && !this.disabled ? this.currentValue : 0;
    }

    loadValue(value) {
        this.userEnabled = value !== 0;
        this.storedValue = value !== 0 ? value : this.storedValue;
        this.currentValue = value !== 0 ? value : this.storedValue;
        if (this.element) this.updateUserEnabledState();
    }
}

class CheckboxControl extends ConfigControl {
    constructor(configPath, label, initialValue) {
        super(configPath, label, initialValue);
    }

    render() {
        const container = document.createElement('div');
        container.className = 'config-row checkbox-row';
        container.innerHTML = `
            <label>
                <input type="checkbox" ${this.initialValue ? 'checked' : ''}>
                ${this.label}
            </label>
        `;
        const checkbox = container.querySelector('input[type="checkbox"]');
        if (checkbox) preventCheckboxEnterDefault(checkbox);
        this.element = container;
        return container;
    }

    getValue() { return this.element.querySelector('input').checked; }
}

class HeaderCheckboxControl extends ConfigControl {
    constructor(configPath, label, initialValue) {
        super(configPath, label, initialValue);
    }

    render() {
        const container = document.createElement('label');
        container.className = 'config-header-toggle';
        container.innerHTML = `
            <input type="checkbox" ${this.initialValue ? 'checked' : ''}>
            <span>${this.label}</span>
        `;
        this.element = container;
        if (this.disabled) this.setDisabled(true);
        return container;
    }

    setDisabled(disabled) {
        this.disabled = disabled;
        if (!this.element) return;
        this.element.classList.toggle('config-header-toggle-disabled', disabled);
        const input = this.element.querySelector('input');
        if (input) {
            input.disabled = disabled;
            if (disabled) {
                input.checked = false;
                this.currentValue = false;
            }
        }
    }

    getValue() { return this.element.querySelector('input').checked; }
}

class LayoutPresetControl extends ConfigControl {
    constructor(configPath, label, initialValue) {
        super(configPath, label, initialValue || 'default');
        this.currentValue = initialValue || 'default';
    }

    render() {
        const container = document.createElement('div');
        container.className = 'layout-preset-control';
        container.innerHTML = '<div class="layout-preset-grid"></div>';
        const grid = container.querySelector('.layout-preset-grid');

        LAYOUT_PRESET_OPTIONS.forEach((option) => {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'layout-preset-btn';
            button.dataset.preset = option.value;
            button.innerHTML = `
                <div class="layout-preset-svg-wrap">${getLayoutPresetSvgMarkup(option.value)}</div>
                <div class="layout-preset-meta">
                    <span class="layout-preset-title">${option.label}</span>
                </div>
            `;
            button.addEventListener('click', () => {
                this.currentValue = option.value;
                this.updateSelectionState();
            });
            grid.appendChild(button);
        });

        this.element = container;
        this.updateSelectionState();
        return container;
    }

    updateSelectionState() {
        if (!this.element) return;
        const buttons = this.element.querySelectorAll('.layout-preset-btn');
        buttons.forEach((button) => {
            if (button.dataset.preset === this.currentValue) {
                button.classList.add('selected');
            } else {
                button.classList.remove('selected');
            }
        });
    }

    loadValue(value) {
        this.currentValue = value || 'default';
        this.updateSelectionState();
    }

    getValue() {
        return this.currentValue;
    }
}

class VorticityControl extends ConfigControl {
    constructor(strengthConfigPath, impactConfigPath, initialStrength, initialImpact, maxStrength = 30, maxImpact = 20) {
        super(strengthConfigPath, 'Vorticity', initialStrength);
        this.impactConfigPath = impactConfigPath;
        this.strengthValue = initialStrength;
        this.impactValue = initialImpact;
        this.maxStrength = maxStrength;
        this.maxImpact = maxImpact;
        this.canvas = null;
        this.ctx = null;
        this.isDraggingUp = false;
        this.isDraggingRight = false;
        this.centerX = 0;
        this.centerY = 0;
        this.arrowInteraction = null;
    }

    render() {
        const container = document.createElement('div');
        container.className = 'vorticity-control';
        container.innerHTML = `
            <div class="vorticity-canvas-container">
                <canvas class="vorticity-canvas"></canvas>
            </div>
            <div class="vorticity-values">
                <div class="vorticity-value-item">
                    <span class="vorticity-value-label">Radius:</span>
                    <span class="vorticity-strength-value">${this.strengthValue.toFixed(1)}</span>
                </div>
                <div class="vorticity-value-item">
                    <span class="vorticity-value-label">Impact:</span>
                    <span class="vorticity-impact-value">${this.impactValue.toFixed(1)}</span>
                </div>
            </div>
        `;

        this.canvas = container.querySelector('.vorticity-canvas');
        this.ctx = this.canvas.getContext('2d');
        this.element = container;

        this.updateCanvasContainerAspect();
        this.setupCanvas();
        this.setupInteraction();
        this.updateInactiveState();

        return container;
    }

    isInactive() {
        return this.strengthValue <= 0;
    }

    updateInactiveState() {
        if (!this.element) return;
        this.element.classList.toggle('vorticity-control-inactive', this.isInactive());
    }

    getValue() {
        return this.strengthValue;
    }

    loadValue(strength, impact) {
        this.strengthValue = strength;
        this.impactValue = impact;
        if (this.element) {
            this.updateDisplay();
            this.updateInactiveState();
            this.draw();
        }
    }

    updateCanvasContainerAspect() {
        const container = this.canvas?.parentElement;
        if (!container) return;
        container.style.aspectRatio = `${getSimCanvasAspectRatio()}`;
    }

    refreshLayout() {
        this.updateCanvasContainerAspect();
        const container = this.canvas?.parentElement;
        if (!container || !this.canvas) return;
        const rect = container.getBoundingClientRect();
        if (rect.width <= 0 || rect.height <= 0) return;
        this.canvas.width = rect.width;
        this.canvas.height = rect.height;
        this.centerX = this.canvas.width / 2;
        this.centerY = this.canvas.height / 2;
        this.draw();
    }

    setupCanvas() {
        setupResizableCanvas(this.canvas, () => {
            this.centerX = this.canvas.width / 2;
            this.centerY = this.canvas.height / 2;
            this.draw();
        });
    }

    getMaxNaturalExtentPx() {
        const strengthPx = WorldScaleService.worldToPixels(this.strengthValue);
        const impactPx = WorldScaleService.worldToPixels(this.impactValue);
        return Math.max(strengthPx, impactPx, 1);
    }

    getDisplayScale() {
        const halfExtent = Math.min(this.centerX, this.centerY);
        return WorldScaleService.getCanvasFitScale(halfExtent, this.getMaxNaturalExtentPx());
    }

    setupInteraction() {
        this.arrowInteraction = setupControlCanvasArrowInteraction(this.canvas, {
            getTips: () => this.getTips(),
            getArrowKeys: () => {
                const keys = ['up'];
                if (!this.isInactive()) keys.push('right');
                return keys;
            },
            onRedraw: () => this.draw(),
        });
        const { updateCursor, isNearArrow, setPinned, refreshGlow } = this.arrowInteraction;

        this.canvas.addEventListener('mousedown', (e) => {
            const pos = getCanvasEventPos(this.canvas, e);
            if (isNearArrow(pos, 'up')) {
                this.isDraggingUp = true;
                setPinned('up', true);
            } else if (!this.isInactive() && isNearArrow(pos, 'right')) {
                this.isDraggingRight = true;
                setPinned('right', true);
            }
        });

        window.addEventListener('mousemove', (e) => {
            if (!this.isDraggingUp && !this.isDraggingRight) return;

            this.canvas.style.cursor = 'crosshair';
            const pos = getCanvasEventPos(this.canvas, e);
            refreshGlow(pos);
            const displayScale = this.getDisplayScale();
            if (this.isDraggingUp) {
                const dy = Math.max(0, this.centerY - pos.y);
                this.strengthValue = Math.max(
                    0,
                    Math.min(this.maxStrength, WorldScaleService.pixelsToWorld(dy / displayScale))
                );
            } else {
                const { up } = this.getTips();
                const dx = Math.max(0, pos.x - up.x);
                this.impactValue = Math.max(
                    1,
                    Math.min(this.maxImpact, WorldScaleService.pixelsToWorld(dx / displayScale))
                );
            }

            this.updateDisplay();
            this.updateInactiveState();
            this.draw();
        });

        window.addEventListener('mouseup', (e) => {
            setPinned('up', false);
            setPinned('right', false);
            this.isDraggingUp = false;
            this.isDraggingRight = false;
            const rect = this.canvas.getBoundingClientRect();
            const overCanvas = e.clientX >= rect.left && e.clientX < rect.right
                && e.clientY >= rect.top && e.clientY < rect.bottom;
            updateCursor(overCanvas ? e : null);
        });
    }

    getTips() {
        const displayScale = this.getDisplayScale();
        const up = {
            x: this.centerX,
            y: this.centerY - WorldScaleService.worldToPixels(this.strengthValue) * displayScale
        };
        return {
            up,
            right: {
                x: up.x + WorldScaleService.worldToPixels(this.impactValue) * displayScale,
                y: up.y
            }
        };
    }

    updateDisplay() {
        const strengthDisplay = this.element.querySelector('.vorticity-strength-value');
        const impactDisplay = this.element.querySelector('.vorticity-impact-value');
        if (strengthDisplay) strengthDisplay.textContent = this.strengthValue.toFixed(1);
        if (impactDisplay) impactDisplay.textContent = this.impactValue.toFixed(1);
    }

    draw() {
        const ctx = this.ctx;
        ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

        const displayScale = this.getDisplayScale();
        updateControlScaleIndicator(this.element, displayScale);

        const inactive = this.isInactive();
        const { up, right } = this.getTips();
        const spiralRadius = this.centerY - up.y;

        if (spiralRadius > 0) {
            ctx.strokeStyle = ControlPalette.spiral;
            ctx.lineWidth = 3;
            ctx.beginPath();
            const turns = 2.5;
            const points = 60;
            for (let i = 0; i <= points; i++) {
                const t = (i / points) * turns * Math.PI * 2;
                const r = (i / points) * spiralRadius;
                const px = this.centerX - r * Math.sin(t);
                const py = this.centerY + r * Math.cos(t);
                if (i === 0) ctx.moveTo(px, py);
                else ctx.lineTo(px, py);
            }
            ctx.stroke();
        }

        drawControlArrow(
            this.ctx,
            { x: this.centerX, y: this.centerY },
            up,
            ControlPalette.radius,
            'up',
            this.arrowInteraction?.getGlow('up') ?? 0
        );
        if (!inactive) {
            drawControlArrow(
                this.ctx,
                up,
                right,
                ControlPalette.impact,
                'left',
                this.arrowInteraction?.getGlow('right') ?? 0
            );
        }
    }
}

class CircleControl extends ConfigControl {
    constructor(radiusConfigPath, pushConfigPath, strengthConfigPath, initialRadius, initialPush, initialStrength) {
        super(radiusConfigPath, 'Interaction', initialRadius);
        this.pushConfigPath = pushConfigPath;
        this.strengthConfigPath = strengthConfigPath;
        this.radiusValue = initialRadius;
        this.pushValue = initialPush;
        this.strengthValue = initialStrength;
        this.maxImpactValue = 20.0;
        this.greyAnchor = 0.005;
        this.blueAnchor = 0.025;
        this.canvas = null;
        this.ctx = null;
        this.isDraggingUp = false;
        this.isDraggingRight = false;
        this.isDraggingDiagonal = false;
        this.centerX = 0;
        this.centerY = 0;
        this.radiusListeners = [];
        this.mouseInputMode = false;
        this.arrowInteraction = null;
    }

    addRadiusListener(listener) {
        this.radiusListeners.push(listener);
    }

    notifyRadiusChange() {
        for (const listener of this.radiusListeners) {
            listener(this.radiusValue);
        }
    }

    setMouseInputMode(mouseInput) {
        this.mouseInputMode = mouseInput;
        if (this.element) this.draw();
    }

    render() {
        const container = document.createElement('div');
        container.className = 'circle-control';
        container.innerHTML = `
            <div class="circle-canvas-container">
                <canvas class="circle-canvas"></canvas>
            </div>
            <div class="circle-values">
                <div class="circle-value-item">
                    <span class="circle-value-label">Radius:</span>
                    <span class="circle-radius-value">${CircleControl.formatRadiusDisplay(this.radiusValue)}</span>
                </div>
                <div class="circle-value-item">
                    <span class="circle-value-label">Effect:</span>
                    <span class="circle-push-value">${this.pushValue.toFixed(1)}x</span>
                </div>
                <div class="circle-value-item">
                    <span class="circle-value-label">Impact:</span>
                    <span class="circle-strength-value">${this.strengthValue.toFixed(1)}</span>
                </div>
            </div>
        `;

        this.canvas = container.querySelector('.circle-canvas');
        this.ctx = this.canvas.getContext('2d');
        this.element = container;

        this.setupCanvas();
        this.setupInteraction();

        return container;
    }

    static formatRadiusDisplay(domainRadius) {
        const scaled = (isNaN(domainRadius) ? 0 : domainRadius) * 100;
        const truncated = Math.floor(scaled * 10) / 10;
        return truncated.toFixed(1);
    }

    getValue() {
        return this.radiusValue;
    }

    loadValue(radius, push, strength) {
        const radiusChanged = radius !== this.radiusValue;
        this.radiusValue = radius;
        this.pushValue = push;
        this.strengthValue = strength;
        if (radiusChanged) this.notifyRadiusChange();
        if (this.element) {
            this.updateDisplay();
            this.draw();
        }
    }

    setupCanvas() {
        setupResizableCanvas(this.canvas, () => {
            this.centerX = this.canvas.width / 2;
            this.centerY = this.canvas.height / 2;
            this.draw();
        });
    }

    getMaxNaturalExtentPx() {
        const radiusPx = this.radiusToPixels(this.radiusValue);
        const pushPx = this.radiusToPixels(this.radiusValue * this.pushValue);
        const strengthPx = WorldScaleService.worldToPixels(this.strengthValue) * Math.SQRT1_2;
        return Math.max(radiusPx, pushPx, strengthPx, 1);
    }

    getDisplayScale() {
        const halfExtent = Math.min(this.centerX, this.centerY);
        return WorldScaleService.getCanvasFitScale(halfExtent, this.getMaxNaturalExtentPx());
    }

    radiusToDisplayPixels(domainRadius) {
        return this.radiusToPixels(domainRadius) * this.getDisplayScale();
    }

    setupInteraction() {
        this.arrowInteraction = setupControlCanvasArrowInteraction(this.canvas, {
            getTips: () => this.getTips(),
            getArrowKeys: () => ['white', 'cyan', 'green'],
            onRedraw: () => this.draw(),
        });
        const { updateCursor, isNearArrow, setPinned, refreshGlow } = this.arrowInteraction;

        this.canvas.addEventListener('mousedown', (e) => {
            const pos = getCanvasEventPos(this.canvas, e);
            if (isNearArrow(pos, 'white')) {
                this.isDraggingUp = true;
                setPinned('white', true);
            } else if (isNearArrow(pos, 'cyan')) {
                this.isDraggingRight = true;
                setPinned('cyan', true);
            } else if (isNearArrow(pos, 'green')) {
                this.isDraggingDiagonal = true;
                setPinned('green', true);
            }
        });

        window.addEventListener('mousemove', (e) => {
            if (!this.isDraggingUp && !this.isDraggingRight && !this.isDraggingDiagonal) return;

            this.canvas.style.cursor = 'crosshair';
            const pos = getCanvasEventPos(this.canvas, e);
            refreshGlow(pos);

            if (this.isDraggingUp) {
                const anchorRadius = this.radiusToDisplayPixels(this.greyAnchor);
                const maxRadius = this.radiusToDisplayPixels(this.blueAnchor);
                const dy = Math.max(0, this.centerY - anchorRadius - pos.y);
                const maxDy = maxRadius - anchorRadius;
                const t = Math.max(0, Math.min(1, dy / maxDy));
                this.radiusValue = this.greyAnchor + t * (this.blueAnchor - this.greyAnchor);
                this.notifyRadiusChange();
            } else if (this.isDraggingRight) {
                const { radiusPx } = this.getTips();
                const maxExtension = this.radiusToDisplayPixels(this.blueAnchor);
                const dx = Math.max(0, pos.x - (this.centerX + radiusPx));
                const maxDx = Math.max(10, maxExtension - radiusPx);
                const t = Math.max(0, Math.min(1, dx / maxDx));
                this.pushValue = 1.0 + t;
            } else {
                const displayScale = this.getDisplayScale();
                const dx = Math.max(0, pos.x - this.centerX);
                const dy = Math.max(0, this.centerY - pos.y);
                const alongWorld = WorldScaleService.pixelsToWorld((dx + dy) / Math.SQRT2 / displayScale);
                this.strengthValue = Math.max(0, Math.min(this.maxImpactValue, alongWorld));
            }

            this.updateDisplay();
            this.draw();
        });

        window.addEventListener('mouseup', (e) => {
            setPinned('white', false);
            setPinned('cyan', false);
            setPinned('green', false);
            this.isDraggingUp = false;
            this.isDraggingRight = false;
            this.isDraggingDiagonal = false;
            const rect = this.canvas.getBoundingClientRect();
            const overCanvas = e.clientX >= rect.left && e.clientX < rect.right
                && e.clientY >= rect.top && e.clientY < rect.bottom;
            updateCursor(overCanvas ? e : null);
        });
    }

    radiusToPixels(domainRadius) {
        const radiusInCells = domainRadius * WorldScaleService.getGridResolution();
        return WorldScaleService.worldToPixels(radiusInCells);
    }

    getTips() {
        const displayScale = this.getDisplayScale();
        const radiusPx = this.radiusToPixels(this.radiusValue) * displayScale;
        const pushPx = this.radiusToPixels(this.radiusValue * this.pushValue) * displayScale;
        const cos45 = Math.SQRT1_2;
        const strengthPx = WorldScaleService.worldToPixels(this.strengthValue) * displayScale;

        return {
            radiusPx,
            white: { x: this.centerX, y: this.centerY - radiusPx },
            cyan: { x: this.centerX + pushPx, y: this.centerY },
            green: {
                x: this.centerX + strengthPx * cos45,
                y: this.centerY - strengthPx * cos45
            }
        };
    }

    updateDisplay() {
        const radiusDisplay = this.element.querySelector('.circle-radius-value');
        const pushDisplay = this.element.querySelector('.circle-push-value');
        const strengthDisplay = this.element.querySelector('.circle-strength-value');
        if (radiusDisplay) radiusDisplay.textContent = CircleControl.formatRadiusDisplay(this.radiusValue);
        if (pushDisplay) pushDisplay.textContent = (isNaN(this.pushValue) ? 1 : this.pushValue).toFixed(1) + 'x';
        if (strengthDisplay) strengthDisplay.textContent = (isNaN(this.strengthValue) ? 0 : this.strengthValue).toFixed(1);
    }

    draw() {
        const ctx = this.ctx;
        ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

        updateControlScaleIndicator(this.element, this.getDisplayScale());

        const tips = this.getTips();
        const pushRadius = this.radiusValue * this.pushValue;

        // Momentum ring only (annulus), never a filled disc over the interior
        if (pushRadius > this.radiusValue) {
            this.drawAnnulus(this.radiusValue, pushRadius, ControlPalette.effectRing);
        }

        // Opaque body circle so the push ring cannot tint the interior
        const circleColor = this.mouseInputMode ? ControlPalette.circleMouse : ControlPalette.circle;
        this.drawFilledCircle(this.radiusValue, circleColor);

        drawControlArrow(
            this.ctx,
            { x: this.centerX, y: this.centerY },
            tips.white,
            ControlPalette.radius,
            'up',
            this.arrowInteraction?.getGlow('white') ?? 0
        );
        drawControlArrow(
            this.ctx,
            { x: this.centerX + tips.radiusPx, y: this.centerY },
            tips.cyan,
            ControlPalette.effect,
            'left',
            this.arrowInteraction?.getGlow('cyan') ?? 0
        );
        drawControlArrow(
            this.ctx,
            { x: this.centerX, y: this.centerY },
            tips.green,
            ControlPalette.impact,
            'diagonal',
            this.arrowInteraction?.getGlow('green') ?? 0
        );
    }

    drawFilledCircle(domainRadius, color) {
        const ctx = this.ctx;
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.arc(this.centerX, this.centerY, this.radiusToDisplayPixels(domainRadius), 0, Math.PI * 2);
        ctx.fill();
    }

    drawAnnulus(innerDomainRadius, outerDomainRadius, fillStyle) {
        const innerPx = this.radiusToDisplayPixels(innerDomainRadius);
        const outerPx = this.radiusToDisplayPixels(outerDomainRadius);
        if (outerPx <= innerPx + 0.5) return;

        const ctx = this.ctx;
        ctx.fillStyle = fillStyle;
        ctx.beginPath();
        ctx.arc(this.centerX, this.centerY, outerPx, 0, Math.PI * 2);
        ctx.arc(this.centerX, this.centerY, innerPx, 0, Math.PI * 2, true);
        ctx.fill('evenodd');
    }
}

class SimModeControl extends ConfigControl {
    static MODES = [
        { value: 'device', label: 'GPU' },
        { value: 'hybrid', label: 'CPU' },
    ];

    constructor(initialValue) {
        super('pipeline', '', initialValue || 'device');
        this.currentValue = initialValue || 'device';
        this.resolutionControl = null;
        this.iterationsControl = null;
        this.overrelaxationControl = null;
        this.onResolutionPreview = null;
    }

    linkResolutionControl(control, onResolutionPreview) {
        this.resolutionControl = control;
        this.onResolutionPreview = onResolutionPreview;
    }

    linkIterationsControl(control) {
        this.iterationsControl = control;
    }

    linkOverrelaxationControl(control) {
        this.overrelaxationControl = control;
    }

    render() {
        const container = document.createElement('div');
        container.className = 'input-mode-control';
        container.innerHTML = '<div class="input-mode-options"></div>';
        const options = container.querySelector('.input-mode-options');

        for (const mode of SimModeControl.MODES) {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'input-mode-btn';
            button.dataset.mode = mode.value;
            button.textContent = mode.label;
            button.addEventListener('click', () => this.selectMode(mode.value));
            options.appendChild(button);
        }

        this.element = container;
        this.updateSelectionState();
        return container;
    }

    updateSelectionState() {
        if (!this.element) return;
        this.element.querySelectorAll('.input-mode-btn').forEach((btn) => {
            btn.classList.toggle('selected', btn.dataset.mode === this.currentValue);
        });
    }

    applyPipelineUi(mode, { useDefaults = false } = {}) {
        if (this.resolutionControl) {
            const range = resolutionRangeForPipeline(mode);
            const value = useDefaults
                ? defaultResolutionForPipeline(mode)
                : clampResolutionForPipeline(this.resolutionControl.currentValue, mode);
            applySliderRange(this.resolutionControl, range, value);
            this.onResolutionPreview?.(value);
        }
        if (this.iterationsControl) {
            const range = iterationRangeForPipeline(mode);
            const value = useDefaults
                ? defaultIterationsForPipeline(mode)
                : clampIterationsForPipeline(this.iterationsControl.currentValue, mode);
            applySliderRange(this.iterationsControl, range, value);
        }
        if (this.overrelaxationControl) {
            this.overrelaxationControl.setDisabled(isGpuSimulatorMode({ pipeline: mode }));
        }
    }

    selectMode(mode) {
        if (mode === this.currentValue) return;
        this.currentValue = mode;
        this.updateSelectionState();
        this.applyPipelineUi(mode, { useDefaults: true });
    }

    loadValue(value) {
        this.currentValue = value || 'device';
        this.updateSelectionState();
    }

    getValue() {
        return this.currentValue;
    }
}

const MOUSE_INPUT_MODES = new Set(['mouse_pull']);

export const INPUT_MODE_INT = {
    hand: 0,
    mouse_pull: 1,
};

export function isMouseInputMode(mode) {
    return MOUSE_INPUT_MODES.has(mode);
}

class InputModeControl extends ConfigControl {
    static MODES = [
        { value: 'hand', label: 'CAMERA' },
        { value: 'mouse_pull', label: 'MOUSE' },
    ];

    constructor(configPath, initialValue) {
        super(configPath, 'Input', initialValue || 'hand');
        this.currentValue = initialValue || 'hand';
        this.handControl = null;
        this.cameraDetected = true;
        this._savedHandModes = null;
    }

    linkHandControl(handControl, cameraDetected) {
        this.handControl = handControl;
        this.cameraDetected = cameraDetected;
        this.updateCameraButtonState();
        this.applyHandControlState();
    }

    linkCircleControl(circleControl) {
        this.circleControl = circleControl;
        this.applyCircleControlState();
    }

    normalizeValue(value) {
        const mode = value || 'hand';
        if (!this.cameraDetected && mode === 'hand') {
            return 'mouse_pull';
        }
        return mode;
    }

    render() {
        const container = document.createElement('div');
        container.className = 'input-mode-control';
        container.innerHTML = '<div class="input-mode-options"></div>';
        const options = container.querySelector('.input-mode-options');

        for (const mode of InputModeControl.MODES) {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'input-mode-btn';
            button.dataset.mode = mode.value;
            button.textContent = mode.label;
            button.addEventListener('click', () => this.selectMode(mode.value));
            options.appendChild(button);
        }

        this.element = container;
        this.updateCameraButtonState();
        this.updateSelectionState();
        return container;
    }

    updateCameraButtonState() {
        if (!this.element) return;
        const cameraBtn = this.element.querySelector('.input-mode-btn[data-mode="hand"]');
        if (!cameraBtn) return;

        const disabled = !this.cameraDetected;
        cameraBtn.disabled = disabled;
        cameraBtn.classList.toggle('input-mode-btn-disabled', disabled);

        if (disabled && this.currentValue === 'hand') {
            this.currentValue = 'mouse_pull';
            this.updateSelectionState();
            this.applyHandControlState();
            this.applyCircleControlState();
        }
    }

    selectMode(mode) {
        if (mode === 'hand' && !this.cameraDetected) return;
        if (mode === this.currentValue) return;
        this.currentValue = mode;
        this.updateSelectionState();
        this.applyHandControlState();
        this.applyCircleControlState();
    }

    applyHandControlState() {
        if (!this.handControl) return;
        const mouseInput = isMouseInputMode(this.currentValue);
        if (mouseInput || !this.cameraDetected) {
            if (mouseInput && !this.handControl.disabled) {
                this._savedHandModes = {
                    left: this.handControl.leftMode,
                    right: this.handControl.rightMode,
                };
            }
            this.handControl.collectWhenDisabled = true;
            this.handControl.setDisabled(true);
        } else {
            this.handControl.collectWhenDisabled = false;
            this.handControl.setDisabled(false);
            const saved = this._savedHandModes;
            if (saved) {
                this.handControl.loadValue(saved.left, saved.right);
                this._savedHandModes = null;
            } else {
                const hands = window.kataraConfig?.hands ?? {};
                this.handControl.loadValue(hands.left ?? 'full', hands.right ?? 'full');
            }
        }
    }

    applyCircleControlState() {
        if (!this.circleControl) return;
        this.circleControl.setMouseInputMode(isMouseInputMode(this.currentValue));
    }

    updateSelectionState() {
        if (!this.element) return;
        this.element.querySelectorAll('.input-mode-btn').forEach((button) => {
            button.classList.toggle('selected', button.dataset.mode === this.currentValue);
        });
    }

    loadValue(value) {
        this.currentValue = this.normalizeValue(value);
        if (this.element) {
            this.updateCameraButtonState();
            this.updateSelectionState();
        }
        this.applyHandControlState();
        this.applyCircleControlState();
    }

    getValue() {
        return this.normalizeValue(this.currentValue);
    }
}

class HandControl extends ConfigControl {
    static CONTENT_ASPECT = 3 / 2;
    static HIT_LINE_THRESHOLD = 8;
    static HIT_DOT_PADDING = 3;
    static DOT_RADIUS_ACTIVE_MIN = 3;
    static DOT_RADIUS_ACTIVE_MAX = 5;
    static DOT_RADIUS_INACTIVE_RATIO = 2.5 / 4;
    static SENSITIVITY_DISPLAY_MIN = 0.5;
    static SENSITIVITY_DISPLAY_MAX = 10;
    static SENSITIVITY_DISPLAY_STEP = 0.5;
    static SENSITIVITY_DISPLAY_SPAN =
        HandControl.SENSITIVITY_DISPLAY_MAX - HandControl.SENSITIVITY_DISPLAY_MIN;

    static snapDisplaySensitivity(display) {
        const step = HandControl.SENSITIVITY_DISPLAY_STEP;
        const snapped = Math.round(display / step) * step;
        return Math.min(
            HandControl.SENSITIVITY_DISPLAY_MAX,
            Math.max(HandControl.SENSITIVITY_DISPLAY_MIN, snapped)
        );
    }

    static internalToDisplaySensitivity(internal) {
        const clamped = Math.max(0, Math.min(1, internal));
        const display = clamped * HandControl.SENSITIVITY_DISPLAY_SPAN + HandControl.SENSITIVITY_DISPLAY_MIN;
        return HandControl.snapDisplaySensitivity(display);
    }

    static displayToInternalSensitivity(display) {
        const clamped = Math.max(
            HandControl.SENSITIVITY_DISPLAY_MIN,
            Math.min(HandControl.SENSITIVITY_DISPLAY_MAX, display)
        );
        return (clamped - HandControl.SENSITIVITY_DISPLAY_MIN) / HandControl.SENSITIVITY_DISPLAY_SPAN;
    }

    constructor(leftConfigPath, rightConfigPath, sensitivityConfigPath, initialLeft, initialRight, initialSensitivity) {
        super(leftConfigPath, 'Hands', initialLeft);
        this.rightConfigPath = rightConfigPath;
        this.sensitivityConfigPath = sensitivityConfigPath;
        this.leftMode = initialLeft || 'full';
        this.rightMode = initialRight || 'full';
        this.sensitivityDisplay = HandControl.internalToDisplaySensitivity(initialSensitivity ?? 0.3);
        this.circleControl = null;
        this.canvas = null;
        this.ctx = null;
        this.sensitivitySlider = null;
        this.glowInteraction = null;
    }

    linkCircleControl(circleControl) {
        this.circleControl = circleControl;
        circleControl.addRadiusListener(() => {
            if (this.element) this.draw();
        });
    }

    getDotRadius(active) {
        const radius = this.circleControl?.radiusValue ?? 0.02;
        const minR = this.circleControl?.greyAnchor ?? 0.005;
        const maxR = this.circleControl?.blueAnchor ?? 0.025;
        const t = Math.max(0, Math.min(1, (radius - minR) / (maxR - minR)));
        const activeR = HandControl.DOT_RADIUS_ACTIVE_MIN +
            t * (HandControl.DOT_RADIUS_ACTIVE_MAX - HandControl.DOT_RADIUS_ACTIVE_MIN);
        return active ? activeR : activeR * HandControl.DOT_RADIUS_INACTIVE_RATIO;
    }

    render() {
        const container = document.createElement('div');
        container.className = 'hand-control';
        container.innerHTML = `
            <div class="hand-canvas-container">
                <canvas class="hand-canvas"></canvas>
            </div>
            <div class="hand-values">
                <div class="hand-value-item">
                    <span class="hand-value-label" style="color:${HAND_COLORS.left}">L:</span>
                    <span class="hand-left-mode">${HAND_MODE_LABELS[this.leftMode]}</span>
                </div>
                <div class="hand-value-item">
                    <span class="hand-value-label" style="color:${HAND_COLORS.right}">R:</span>
                    <span class="hand-right-mode">${HAND_MODE_LABELS[this.rightMode]}</span>
                </div>
            </div>
            <div class="hand-sliders">
                <div class="config-row hand-sensitivity-row">
                    <label>Sensitivity</label>
                    <input type="range" min="${HandControl.SENSITIVITY_DISPLAY_MIN}" max="${HandControl.SENSITIVITY_DISPLAY_MAX}" step="${HandControl.SENSITIVITY_DISPLAY_STEP}" value="${this.sensitivityDisplay}">
                    <span class="value-display hand-sensitivity-value">${formatSliderValue(this.sensitivityDisplay, HandControl.SENSITIVITY_DISPLAY_STEP)}</span>
                </div>
            </div>
        `;

        this.canvas = container.querySelector('.hand-canvas');
        this.ctx = this.canvas.getContext('2d');
        this.element = container;

        this.sensitivitySlider = container.querySelector('.hand-sensitivity-row');
        bindSliderRow(this.sensitivitySlider, {
            onChange: (value) => {
                this.sensitivityDisplay = value;
                this.updateSensitivitySliderState();
            },
            format: (value) => formatSliderValue(value, HandControl.SENSITIVITY_DISPLAY_STEP),
        });
        this.updateSensitivitySliderState();

        this.setupCanvas();
        this.setupInteraction();

        if (this.disabled) this.setDisabled(true);
        return container;
    }

    setDisabled(disabled) {
        this.disabled = disabled;
        if (!this.element) return;
        this.element.classList.toggle('hand-control-disabled', disabled);
        if (this.canvas) {
            this.canvas.style.cursor = disabled ? 'not-allowed' : 'default';
            this.canvas.style.pointerEvents = disabled ? 'none' : '';
        }
        this.updateSensitivitySliderState();
        if (disabled) {
            this.leftMode = 'none';
            this.rightMode = 'none';
            this.updateDisplay();
            this.draw();
        }
    }

    updateSensitivitySliderState() {
        if (!this.sensitivitySlider) return;
        this.sensitivitySlider.classList.toggle('config-row-disabled', this.disabled);
        this.sensitivitySlider.classList.toggle(
            'control-display-clamped',
            !this.disabled && this.sensitivityDisplay >= HandControl.SENSITIVITY_DISPLAY_MAX
        );
        const input = this.sensitivitySlider.querySelector('input');
        if (input) input.disabled = this.disabled;
    }

    getValue() {
        return this.leftMode;
    }

    loadValue(left, right, sensitivity) {
        if (this.disabled) {
            left = 'none';
            right = 'none';
        }
        this.leftMode = left || 'full';
        this.rightMode = right || 'full';
        if (sensitivity !== undefined) {
            this.sensitivityDisplay = HandControl.internalToDisplaySensitivity(sensitivity);
        }
        if (this.element) {
            const sensitivityInput = this.sensitivitySlider?.querySelector('input');
            const sensitivityLabel = this.sensitivitySlider?.querySelector('.hand-sensitivity-value');
            if (sensitivityInput) sensitivityInput.value = this.sensitivityDisplay;
            if (sensitivityLabel) {
                sensitivityLabel.textContent = formatSliderValue(
                    this.sensitivityDisplay,
                    HandControl.SENSITIVITY_DISPLAY_STEP
                );
            }
            this.updateSensitivitySliderState();
            this.updateDisplay();
            this.draw();
        }
    }

    setupCanvas() {
        setupResizableCanvas(this.canvas, () => this.draw());
    }

    getHandTransform(ox, oy, areaW, areaH, mirror) {
        const pad = 0.1;
        const usableW = areaW * (1 - 2 * pad);
        const usableH = areaH * (1 - 2 * pad);
        const offsetX = ox + areaW * pad;
        const offsetY = oy + areaH * pad;
        return {
            tx: (lm) => {
                let nx = lm.x;
                if (mirror) nx = 1 - nx;
                return offsetX + nx * usableW;
            },
            ty: (lm) => offsetY + lm.y * usableH,
        };
    }

    hitTestHandShape(pos, ox, oy, areaW, areaH, mode, mirror) {
        const { tx, ty } = this.getHandTransform(ox, oy, areaW, areaH, mirror);
        const activeSet = new Set(ACTIVE_LANDMARKS_BY_MODE[mode]);

        for (const [i, j] of HAND_CONNECTIONS) {
            const p1 = { x: tx(HAND_LANDMARKS[i]), y: ty(HAND_LANDMARKS[i]) };
            const p2 = { x: tx(HAND_LANDMARKS[j]), y: ty(HAND_LANDMARKS[j]) };
            if (pointToSegmentDistance(pos, p1, p2) <= HandControl.HIT_LINE_THRESHOLD) {
                return true;
            }
        }

        for (let i = 0; i < HAND_LANDMARKS.length; i++) {
            const active = activeSet.has(i);
            const r = this.getDotRadius(active) + HandControl.HIT_DOT_PADDING;
            const dx = pos.x - tx(HAND_LANDMARKS[i]);
            const dy = pos.y - ty(HAND_LANDMARKS[i]);
            if (dx * dx + dy * dy <= r * r) {
                return true;
            }
        }

        return false;
    }

    getActiveHandKey(pos) {
        if (!pos || this.disabled) return null;
        const content = this.getContentRect();
        const halfW = content.width / 2;
        if (this.hitTestHandShape(pos, content.x, content.y, halfW, content.height, this.leftMode, true)) {
            return 'left';
        }
        if (this.hitTestHandShape(
            pos, content.x + halfW, content.y, halfW, content.height, this.rightMode, false
        )) {
            return 'right';
        }
        return null;
    }

    setupInteraction() {
        this.glowInteraction = setupControlCanvasGlowInteraction(this.canvas, {
            getKeys: () => ['left', 'right'],
            getActiveKey: (pos) => this.getActiveHandKey(pos),
            onRedraw: () => this.draw(),
        });

        this.canvas.style.cursor = 'default';

        this.canvas.addEventListener('mousemove', (e) => {
            if (this.disabled) return;
            const pos = getCanvasEventPos(this.canvas, e);
            this.canvas.style.cursor = this.getActiveHandKey(pos) ? 'crosshair' : 'default';
        });
        this.canvas.addEventListener('mouseleave', () => {
            if (!this.disabled) this.canvas.style.cursor = 'default';
        });

        this.canvas.addEventListener('click', (e) => {
            if (this.disabled) return;
            const handKey = this.getActiveHandKey(getCanvasEventPos(this.canvas, e));
            if (!handKey) return;

            if (handKey === 'left') {
                const idx = HAND_MODES.indexOf(this.leftMode);
                this.leftMode = HAND_MODES[(idx + 1) % HAND_MODES.length];
            } else {
                const idx = HAND_MODES.indexOf(this.rightMode);
                this.rightMode = HAND_MODES[(idx + 1) % HAND_MODES.length];
            }

            this.updateDisplay();
            this.draw();
        });
    }

    updateDisplay() {
        const leftDisplay = this.element.querySelector('.hand-left-mode');
        const rightDisplay = this.element.querySelector('.hand-right-mode');
        if (leftDisplay) leftDisplay.textContent = HAND_MODE_LABELS[this.leftMode];
        if (rightDisplay) rightDisplay.textContent = HAND_MODE_LABELS[this.rightMode];
    }

    getContentRect() {
        return fitCenteredRect(this.canvas.width, this.canvas.height, HandControl.CONTENT_ASPECT);
    }

    draw() {
        const ctx = this.ctx;
        const w = this.canvas.width;
        const h = this.canvas.height;
        ctx.clearRect(0, 0, w, h);

        const content = this.getContentRect();
        this.drawHand(
            content.x, content.y, content.width / 2, content.height,
            this.leftMode, HAND_COLORS.left, true,
            this.glowInteraction?.getGlow('left') ?? 0
        );
        this.drawHand(
            content.x + content.width / 2, content.y, content.width / 2, content.height,
            this.rightMode, HAND_COLORS.right, false,
            this.glowInteraction?.getGlow('right') ?? 0
        );
    }

    drawHand(ox, oy, areaW, areaH, mode, activeColor, mirror, glowT = 0) {
        const ctx = this.ctx;
        const activeSet = new Set(ACTIVE_LANDMARKS_BY_MODE[mode]);
        const grey = HAND_COLORS.grey;
        const greyDot = HAND_COLORS.greyDot;
        const { tx, ty } = this.getHandTransform(ox, oy, areaW, areaH, mirror);
        const lms = HAND_LANDMARKS;

        ctx.lineWidth = 2;
        for (const [i, j] of HAND_CONNECTIONS) {
            const bothActive = mode !== 'joints' && activeSet.has(i) && activeSet.has(j);
            if (bothActive) continue;
            ctx.strokeStyle = grey;
            ctx.beginPath();
            ctx.moveTo(tx(lms[i]), ty(lms[i]));
            ctx.lineTo(tx(lms[j]), ty(lms[j]));
            ctx.stroke();
        }

        ctx.save();
        applyControlCanvasGlow(ctx, glowT);
        for (const [i, j] of HAND_CONNECTIONS) {
            const bothActive = mode !== 'joints' && activeSet.has(i) && activeSet.has(j);
            if (!bothActive) continue;
            ctx.strokeStyle = activeColor;
            ctx.beginPath();
            ctx.moveTo(tx(lms[i]), ty(lms[i]));
            ctx.lineTo(tx(lms[j]), ty(lms[j]));
            ctx.stroke();
        }
        ctx.restore();

        for (let i = 0; i < lms.length; i++) {
            if (activeSet.has(i)) continue;
            ctx.fillStyle = greyDot;
            const r = this.getDotRadius(false);
            ctx.beginPath();
            ctx.arc(tx(lms[i]), ty(lms[i]), r, 0, Math.PI * 2);
            ctx.fill();
        }

        ctx.save();
        applyControlCanvasGlow(ctx, glowT);
        for (let i = 0; i < lms.length; i++) {
            if (!activeSet.has(i)) continue;
            ctx.fillStyle = activeColor;
            const r = this.getDotRadius(true);
            ctx.beginPath();
            ctx.arc(tx(lms[i]), ty(lms[i]), r, 0, Math.PI * 2);
            ctx.fill();
        }
        ctx.restore();
    }
}

class EnvironmentControl extends ConfigControl {
    static EDGE_BITS = { left: 8, top: 4, bottom: 2, right: 1 };
    static SIDE_TO_EDGE = { 0: 8, 1: 4, 2: 2, 3: 1 };
    // Canvas Y is down; sim Y is up. Widget side ids follow screen position (1=bottom, 2=top).
    static EDGES = [
        { key: 'left', bit: 8, side: 0 },
        { key: 'top', bit: 4, side: 2 },
        { key: 'bottom', bit: 2, side: 1 },
        { key: 'right', bit: 1, side: 3 },
    ];

    static verticalSideUiToSim(uiSide) {
        if (uiSide === 1) return 2;
        if (uiSide === 2) return 1;
        return uiSide;
    }

    static verticalSideSimToUi(simSide) {
        return EnvironmentControl.verticalSideUiToSim(simSide);
    }

    constructor(initialEdges, initialWindTunnel = {}) {
        super('simulation.edges', 'Environment', initialEdges);
        this.edgesMask = initialEdges ?? 14;
        this.windSide = initialWindTunnel.side ?? -1;
        this.velocityValue = initialWindTunnel.velocity ?? 1;
        const start = initialWindTunnel.startPosition ?? 0.45;
        const end = initialWindTunnel.endPosition ?? 0.55;
        this.widthValue = Math.max(0.01, Math.min(0.2, end - start));
        this.canvas = null;
        this.ctx = null;
        this.layout = null;
        this.velocitySlider = null;
        this.widthSlider = null;
        this.glowInteraction = null;
    }

    updateCanvasContainerAspect() {
        const container = this.canvas?.parentElement;
        if (!container) return;
        container.style.aspectRatio = `${getSimCanvasAspectRatio()}`;
    }

    refreshLayout() {
        this.updateCanvasContainerAspect();
        const container = this.canvas?.parentElement;
        if (!container || !this.canvas) return;
        const rect = container.getBoundingClientRect();
        if (rect.width <= 0 || rect.height <= 0) return;
        this.canvas.width = rect.width;
        this.canvas.height = rect.height;
        this.layout = this.computeLayout();
        this.draw();
    }

    render() {
        const container = document.createElement('div');
        container.className = 'environment-control';
        container.innerHTML = `
            <div class="environment-canvas-container">
                <canvas class="environment-canvas"></canvas>
            </div>
            <div class="environment-sliders">
                <div class="config-row environment-velocity-row">
                    <label>Velocity</label>
                    <input type="range" min="0.01" max="3" step="0.01" value="${this.velocityValue}">
                    <span class="value-display environment-velocity-value">${this.velocityValue.toFixed(2)}</span>
                </div>
                <div class="config-row environment-width-row">
                    <label>Width</label>
                    <input type="range" min="0.01" max="0.2" step="0.01" value="${this.widthValue}">
                    <span class="value-display environment-width-value">${this.widthValue.toFixed(2)}</span>
                </div>
            </div>
        `;

        this.canvas = container.querySelector('.environment-canvas');
        this.ctx = this.canvas.getContext('2d');
        this.element = container;

        this.velocitySlider = container.querySelector('.environment-velocity-row');
        bindSliderRow(this.velocitySlider, {
            onChange: (value) => {
                this.velocityValue = value;
                this.draw();
            },
            format: (value) => formatSliderValue(value, 0.01),
        });

        this.widthSlider = container.querySelector('.environment-width-row');
        bindSliderRow(this.widthSlider, {
            onChange: (value) => {
                this.widthValue = value;
                this.draw();
            },
            format: (value) => formatSliderValue(value, 0.01),
        });

        this.updateCanvasContainerAspect();
        this.setupCanvas();
        this.setupInteraction();
        this.updateSliderState();

        return container;
    }

    getValue() {
        return this.edgesMask;
    }

    loadValue(edges, side, velocity, startPosition, endPosition) {
        if (edges !== undefined) this.edgesMask = edges;
        if (side !== undefined) this.windSide = EnvironmentControl.verticalSideSimToUi(side);
        if (velocity !== undefined) this.velocityValue = velocity;
        if (startPosition !== undefined && endPosition !== undefined) {
            this.widthValue = Math.max(0.01, Math.min(0.2, endPosition - startPosition));
        }
        if (this.element) {
            const velocityInput = this.velocitySlider?.querySelector('input');
            const velocityDisplay = this.velocitySlider?.querySelector('.environment-velocity-value');
            if (velocityInput) velocityInput.value = this.velocityValue;
            if (velocityDisplay) velocityDisplay.textContent = this.velocityValue.toFixed(2);

            const widthInput = this.widthSlider?.querySelector('input');
            const widthDisplay = this.widthSlider?.querySelector('.environment-width-value');
            if (widthInput) widthInput.value = this.widthValue;
            if (widthDisplay) widthDisplay.textContent = this.widthValue.toFixed(2);

            this.updateSliderState();
            this.draw();
        }
    }

    updateSliderState() {
        const disabled = this.windSide === -1;
        for (const row of [this.velocitySlider, this.widthSlider]) {
            if (!row) continue;
            row.classList.toggle('config-row-disabled', disabled);
            const input = row.querySelector('input');
            if (input) input.disabled = disabled;
        }
    }

    setupCanvas() {
        setupResizableCanvas(this.canvas, () => {
            this.updateCanvasContainerAspect();
            this.layout = this.computeLayout();
            this.draw();
        });
    }

    setupInteraction() {
        this.glowInteraction = setupControlCanvasGlowInteraction(this.canvas, {
            getKeys: () => [0, 1, 2, 3],
            getActiveKey: (pos) => this.getActiveArrowSide(pos),
            onRedraw: () => this.draw(),
        });

        this.canvas.addEventListener('mousemove', (e) => {
            const pos = getCanvasEventPos(this.canvas, e);
            if (this.getActiveArrowSide(pos) !== null) {
                this.canvas.style.cursor = 'crosshair';
            } else if (this.hitTestEdge(pos) !== null) {
                this.canvas.style.cursor = 'pointer';
            } else {
                this.canvas.style.cursor = 'default';
            }
        });
        this.canvas.addEventListener('mouseleave', () => {
            this.canvas.style.cursor = 'default';
        });

        this.canvas.addEventListener('click', (e) => {
            this.handleClick(getCanvasEventPos(this.canvas, e));
        });
    }

    handleClick(pos) {
        const edgeHit = this.hitTestEdge(pos);
        if (edgeHit !== null) {
            this.edgesMask ^= edgeHit.bit;
            const edgeStillActive = (this.edgesMask & edgeHit.bit) !== 0;
            if (this.windSide === edgeHit.side && !edgeStillActive) {
                this.windSide = -1;
                this.updateSliderState();
            }
            this.draw();
            return;
        }

        const arrowHit = this.hitTestArrow(pos);
        if (arrowHit !== null) {
            if (this.windSide === arrowHit) {
                this.windSide = -1;
            } else {
                this.windSide = arrowHit;
                const simSide = EnvironmentControl.verticalSideUiToSim(arrowHit);
                this.edgesMask |= EnvironmentControl.SIDE_TO_EDGE[simSide];
            }
            this.updateSliderState();
            this.draw();
        }
    }

    computeLayout() {
        const w = this.canvas.width;
        const h = this.canvas.height;
        const aspect = getSimCanvasAspectRatio();
        const canvasPad = Math.max(12, Math.min(w, h) * 0.10);
        const edgeThickness = Math.max(3, Math.min(w, h) * 0.026);
        const inner = fitCenteredRect(
            w - (canvasPad + edgeThickness) * 2,
            h - (canvasPad + edgeThickness) * 2,
            aspect
        );
        inner.x += canvasPad + edgeThickness;
        inner.y += canvasPad + edgeThickness;
        const outer = {
            x: inner.x - edgeThickness,
            y: inner.y - edgeThickness,
            width: inner.width + edgeThickness * 2,
            height: inner.height + edgeThickness * 2,
        };
        return { inner, outer, edgeThickness };
    }

    getEdgePolygons() {
        const { inner, outer } = this.layout;
        return {
            left: [
                { x: outer.x, y: outer.y },
                { x: outer.x, y: outer.y + outer.height },
                { x: inner.x, y: inner.y + inner.height },
                { x: inner.x, y: inner.y },
            ],
            top: [
                { x: outer.x, y: outer.y },
                { x: outer.x + outer.width, y: outer.y },
                { x: inner.x + inner.width, y: inner.y },
                { x: inner.x, y: inner.y },
            ],
            bottom: [
                { x: inner.x, y: inner.y + inner.height },
                { x: inner.x + inner.width, y: inner.y + inner.height },
                { x: outer.x + outer.width, y: outer.y + outer.height },
                { x: outer.x, y: outer.y + outer.height },
            ],
            right: [
                { x: inner.x + inner.width, y: inner.y },
                { x: outer.x + outer.width, y: outer.y },
                { x: outer.x + outer.width, y: outer.y + outer.height },
                { x: inner.x + inner.width, y: inner.y + inner.height },
            ],
        };
    }

    getArrowSpecs() {
        const { inner } = this.layout;
        const velocityMin = 0.01;
        const velocityMax = 3.0;
        const velocityT = Math.max(0, Math.min(1, (this.velocityValue - velocityMin) / (velocityMax - velocityMin)));
        const minFracToCenter = 0.30;
        const maxFracToCenter = 0.70;
        const reachFrac = minFracToCenter + velocityT * (maxFracToCenter - minFracToCenter);
        const lenX = Math.max(14, (inner.width * 0.5) * reachFrac);
        const lenY = Math.max(14, (inner.height * 0.5) * reachFrac);
        return [
            {
                side: 0,
                from: { x: inner.x, y: inner.y + inner.height / 2 },
                to: { x: inner.x + lenX, y: inner.y + inner.height / 2 },
            },
            {
                side: 2,
                from: { x: inner.x + inner.width / 2, y: inner.y },
                to: { x: inner.x + inner.width / 2, y: inner.y + lenY },
            },
            {
                side: 1,
                from: { x: inner.x + inner.width / 2, y: inner.y + inner.height },
                to: { x: inner.x + inner.width / 2, y: inner.y + inner.height - lenY },
            },
            {
                side: 3,
                from: { x: inner.x + inner.width, y: inner.y + inner.height / 2 },
                to: { x: inner.x + inner.width - lenX, y: inner.y + inner.height / 2 },
            },
        ];
    }

    pointToSegmentDistance(point, a, b) {
        const abX = b.x - a.x;
        const abY = b.y - a.y;
        const apX = point.x - a.x;
        const apY = point.y - a.y;
        const abLenSq = abX * abX + abY * abY;
        if (abLenSq === 0) {
            const dx = point.x - a.x;
            const dy = point.y - a.y;
            return Math.sqrt(dx * dx + dy * dy);
        }
        const t = Math.max(0, Math.min(1, (apX * abX + apY * abY) / abLenSq));
        const closestX = a.x + t * abX;
        const closestY = a.y + t * abY;
        const dx = point.x - closestX;
        const dy = point.y - closestY;
        return Math.sqrt(dx * dx + dy * dy);
    }

    pointInPolygon(point, polygon) {
        let inside = false;
        for (let i = 0, j = polygon.length - 1; i < polygon.length; j = i++) {
            if (this.pointToSegmentDistance(point, polygon[j], polygon[i]) <= 1.0) {
                return true;
            }
            const xi = polygon[i].x;
            const yi = polygon[i].y;
            const xj = polygon[j].x;
            const yj = polygon[j].y;
            const intersect = ((yi > point.y) !== (yj > point.y)) &&
                (point.x < ((xj - xi) * (point.y - yi)) / (yj - yi) + xi);
            if (intersect) inside = !inside;
        }
        return inside;
    }

    hitTestEdge(pos) {
        const polygons = this.getEdgePolygons();
        for (const edge of EnvironmentControl.EDGES) {
            if (this.pointInPolygon(pos, polygons[edge.key])) {
                return edge;
            }
        }
        return null;
    }

    getArrowDimensions(arrow) {
        const axisSize = (arrow.side === 0 || arrow.side === 3)
            ? this.layout.inner.height
            : this.layout.inner.width;
        const widthPx = axisSize * this.widthValue;
        const baseWidth = Math.max(this.layout.edgeThickness * 0.42, widthPx * 0.92);
        return {
            baseWidth,
            headWidth: Math.max(baseWidth * 1.15, this.layout.edgeThickness * 0.62),
            headLength: Math.max(11, this.layout.edgeThickness * 0.85),
        };
    }

    getActiveArrowSide(pos) {
        if (!pos || !this.layout) return null;
        for (const arrow of this.getArrowSpecs()) {
            const dims = this.getArrowDimensions(arrow);
            const { full } = buildEnvironmentArrowGeometry(
                arrow.from,
                arrow.to,
                dims.baseWidth,
                dims.headWidth,
                dims.headLength
            );
            if (isPointInPolygon(pos, full)) return arrow.side;
        }
        return null;
    }

    hitTestArrow(pos) {
        return this.getActiveArrowSide(pos);
    }

    draw() {
        if (!this.ctx || !this.layout) return;
        const ctx = this.ctx;
        const w = this.canvas.width;
        const h = this.canvas.height;
        ctx.clearRect(0, 0, w, h);

        const polygons = this.getEdgePolygons();

        for (const edge of EnvironmentControl.EDGES) {
            const active = (this.edgesMask & edge.bit) !== 0;
            this.fillPolygon(polygons[edge.key], active ? ControlPalette.radius : ControlPalette.circle);
        }

        const arrowColorOff = Palette.greyInactive;
        for (const arrow of this.getArrowSpecs()) {
            const active = this.windSide === arrow.side;
            const glowT = this.glowInteraction?.getGlow(arrow.side) ?? 0;
            const dims = this.getArrowDimensions(arrow);
            this.drawArrow(
                arrow.from,
                arrow.to,
                active ? ControlPalette.impact : arrowColorOff,
                dims.baseWidth,
                dims.headWidth,
                dims.headLength,
                glowT
            );
        }
    }

    fillPolygon(points, color) {
        const ctx = this.ctx;
        ctx.fillStyle = color;
        ctx.strokeStyle = color;
        ctx.lineWidth = 1;
        ctx.lineJoin = 'round';
        ctx.beginPath();
        ctx.moveTo(points[0].x, points[0].y);
        for (let i = 1; i < points.length; i++) {
            ctx.lineTo(points[i].x, points[i].y);
        }
        ctx.closePath();
        ctx.fill();
        ctx.stroke();
    }

    drawArrow(from, tip, color, baseWidth = 8, headWidth = 14, headLength = 12, glowT = 0) {
        const ctx = this.ctx;
        const { body, head } = buildEnvironmentArrowGeometry(from, tip, baseWidth, headWidth, headLength);

        const fillPolygon = (points) => {
            if (points.length < 3) return;
            ctx.beginPath();
            ctx.moveTo(points[0].x, points[0].y);
            for (let i = 1; i < points.length; i++) {
                ctx.lineTo(points[i].x, points[i].y);
            }
            ctx.closePath();
            ctx.fill();
        };

        ctx.save();
        ctx.fillStyle = color;
        fillPolygon(body);
        ctx.restore();

        ctx.save();
        applyControlCanvasGlow(ctx, glowT);
        ctx.fillStyle = color;
        fillPolygon(head);
        ctx.restore();
    }
}

function collectControlValue(control, values) {
    if (control.disabled && !control.collectWhenDisabled) return;
    if (control instanceof VorticityControl) {
        setNestedValue(values, control.configPath, control.strengthValue);
        setNestedValue(values, control.impactConfigPath, control.impactValue);
    } else if (control instanceof CircleControl) {
        setNestedValue(values, control.configPath, control.radiusValue);
        setNestedValue(values, control.pushConfigPath, control.pushValue);
        setNestedValue(values, control.strengthConfigPath, control.strengthValue);
    } else if (control instanceof HandControl) {
        setNestedValue(values, control.configPath, control.leftMode);
        setNestedValue(values, control.rightConfigPath, control.rightMode);
        setNestedValue(
            values,
            control.sensitivityConfigPath,
            HandControl.displayToInternalSensitivity(control.sensitivityDisplay)
        );
    } else if (control instanceof EnvironmentControl) {
        setNestedValue(values, control.configPath, control.edgesMask);
        setNestedValue(
            values,
            'simulation.windTunnel.side',
            EnvironmentControl.verticalSideUiToSim(control.windSide)
        );
        setNestedValue(values, 'simulation.windTunnel.velocity', control.velocityValue);
        const halfWidth = control.widthValue / 2;
        setNestedValue(values, 'simulation.windTunnel.startPosition', 0.5 - halfWidth);
        setNestedValue(values, 'simulation.windTunnel.endPosition', 0.5 + halfWidth);
    } else {
        setNestedValue(values, control.configPath, control.getValue());
    }
}

function loadControlValue(control, config) {
    if (control instanceof VorticityControl) {
        const strength = getNestedValue(config, control.configPath);
        const impact = getNestedValue(config, control.impactConfigPath);
        if (strength !== undefined && impact !== undefined) control.loadValue(strength, impact);
    } else if (control instanceof CircleControl) {
        const radius = getNestedValue(config, control.configPath);
        const push = getNestedValue(config, control.pushConfigPath);
        const strength = getNestedValue(config, control.strengthConfigPath);
        if (radius !== undefined || push !== undefined || strength !== undefined) {
            control.loadValue(
                radius ?? control.radiusValue,
                push ?? control.pushValue,
                strength ?? control.strengthValue
            );
        }
    } else if (control instanceof SimModeControl) {
        control.loadValue(config.pipeline ?? 'device');
    } else if (control instanceof InputModeControl) {
        control.loadValue(config.inputMode ?? 'hand');
    } else if (control instanceof HandControl) {
        const sensitivity = getNestedValue(config, control.sensitivityConfigPath);
        if (control.disabled) {
            control.loadValue('none', 'none', sensitivity);
        } else {
            control.loadValue(
                getNestedValue(config, control.configPath),
                getNestedValue(config, control.rightConfigPath),
                sensitivity
            );
        }
    } else if (control instanceof EnvironmentControl) {
        const windTunnel = config.simulation?.windTunnel ?? {};
        control.loadValue(
            getNestedValue(config, control.configPath),
            windTunnel.side,
            windTunnel.velocity,
            windTunnel.startPosition,
            windTunnel.endPosition
        );
    } else if (control instanceof HeaderCheckboxControl && control.disabled) {
        control.currentValue = false;
        const checkbox = control.element?.querySelector('input[type="checkbox"]');
        if (checkbox) checkbox.checked = false;
    } else if (control instanceof LayoutPresetControl) {
        const value = getNestedValue(config, control.configPath);
        if (value !== undefined) control.loadValue(value);
    } else {
        const value = getNestedValue(config, control.configPath);
        if (value === undefined) return;
        control.currentValue = value;
        if (!control.element) return;
        if (control instanceof ToggleSliderControl) {
            control.loadValue(value);
        } else if (control instanceof SliderControl) {
            const input = control.element.querySelector('input[type="range"]');
            const display = control.element.querySelector('.value-display');
            if (input) input.value = value;
            if (display) display.textContent = formatSliderValue(value, control.step);
        } else if (control instanceof CheckboxControl || control instanceof HeaderCheckboxControl) {
            const checkbox = control.element.querySelector('input[type="checkbox"]');
            if (checkbox) checkbox.checked = value;
        }
    }
}

class ConfigSection {
    constructor(title, options = {}) {
        this.title = title;
        this.controls = [];
        this.headerControls = [];
        this.layout = options.layout ?? 'column';
        this.column = options.column ?? 'left';
        this.placement = options.placement ?? 'top';
        this.disabled = options.disabled ?? false;
    }

    addControl(control) {
        this.controls.push(control);
    }

    addHeaderControl(control) {
        this.headerControls.push(control);
    }

    getAllControls() {
        return [...this.headerControls, ...this.controls];
    }

    render() {
        const section = document.createElement('div');
        section.className = 'config-section';
        if (this.disabled) section.classList.add('config-section-disabled');
        section.innerHTML = `
            <div class="config-section-header">
                <h3>${this.title}</h3>
                <div class="config-section-header-controls"></div>
            </div>
            <div class="config-section-content"></div>
        `;

        const headerControls = section.querySelector('.config-section-header-controls');
        this.headerControls.forEach(control => headerControls.appendChild(control.render()));

        const content = section.querySelector('.config-section-content');
        const renderControls = (container) => this.controls.forEach(control => container.appendChild(control.render()));

        if (this.layout === 'grid2' || this.layout === 'grid4') {
            const grid = document.createElement('div');
            grid.className = this.layout === 'grid4' ? 'config-grid config-grid-4' : 'config-grid';
            renderControls(grid);
            content.appendChild(grid);
        } else if (this.layout === 'canvasRow') {
            const row = document.createElement('div');
            row.className = 'config-canvas-row';
            renderControls(row);
            content.appendChild(row);
        } else {
            renderControls(content);
        }

        return section;
    }

    collectValues() {
        const values = {};
        this.getAllControls().forEach(control => collectControlValue(control, values));
        return values;
    }
}

function formatTargetIndex(index) {
    return String(index).padStart(2, '0');
}

function isShaderEditingLocked(index) {
    return index === 3;
}

function getBuiltinShaderDisplayName(index) {
    if (index === 3) {
        return 'IMG';
    }
    const names = {
        0: 'PRESSURE', 1: 'DENSITY', 2: 'FLUID', 4: 'DIVERGENCE', 5: 'NORMAL',
        6: 'HEATMAP', 7: 'BLOOM', 8: 'SHIMMER', 9: 'PLASMA', 10: 'CHROME', 11: 'VAPOR',
    };
    return names[index] || `TARGET_${index}`;
}

function getShaderTargetName(index, app) {
    const customName = window.kataraConfig?.customViews?.[String(index)]?.displayName;
    if (customName) {
        return customName;
    }
    if (index === 3) {
        return app?.getInkDisplayName?.() ?? 'INK';
    }
    const names = {
        0: 'PRESSURE', 1: 'DENSITY', 2: 'FLUID', 4: 'DIVERGENCE', 5: 'NORMAL',
        6: 'HEATMAP', 7: 'BLOOM', 8: 'SHIMMER', 9: 'PLASMA', 10: 'CHROME', 11: 'VAPOR',
    };
    return names[index] || `TARGET_${index}`;
}

class SettingsPanel {
    static VIEW_TARGET_COUNT = 12;

    constructor(parentApp) {
        this.parentApp = parentApp;
        this.panelElement = null;
        this.sections = [];
        this.isOpen = false;
        this.wasPausedBeforeOpen = false;
        this.originalConfig = null;
        this.boundKeyHandler = null;
        this.activeTab = 'settings';
        this.shaderTargetIndex = 0;
        this.shaderDrafts = {};
        this.baselineShaderDrafts = {};
        this.shaderDisplayNameDrafts = {};
        this.baselineShaderDisplayNames = {};
        this.shaderRenamingIndex = null;
        this.shaderEditorEditing = false;
        this.shaderEditorSessionDraft = '';
        this.shaderError = null;
        this.settingsTabPane = null;
        this.shaderTabPane = null;
        this.shaderExplorerElement = null;
        this.shaderEditorToolbarElement = null;
        this.shaderEditorEditButton = null;
        this.shaderEditorSaveButton = null;
        this.shaderEditorCancelButton = null;
        this.shaderEditorShellElement = null;
        this.shaderEditorElement = null;
        this.shaderErrorElement = null;
    }

    readShaderSourceFromWasm(index) {
        const mod = this.parentApp.module;
        if (!mod?._getViewSource) {
            return '';
        }
        const ptr = mod._getViewSource(index);
        if (!ptr) {
            return '';
        }
        const text = mod.UTF8ToString(ptr);
        mod._freeShaderString(ptr);
        return text;
    }

    initializeShaderDisplayNames() {
        this.shaderDisplayNameDrafts = {};
        this.baselineShaderDisplayNames = {};
        for (let index = 0; index < SettingsPanel.VIEW_TARGET_COUNT; index++) {
            const key = String(index);
            const customName = isShaderEditingLocked(index)
                ? undefined
                : (this.originalConfig?.customViews?.[key]?.displayName
                    ?? window.kataraConfig?.customViews?.[key]?.displayName);
            const name = customName || getBuiltinShaderDisplayName(index);
            this.shaderDisplayNameDrafts[index] = name;
            this.baselineShaderDisplayNames[index] = name;
        }
    }

    getShaderDisplayName(index) {
        return this.shaderDisplayNameDrafts[index] ?? getBuiltinShaderDisplayName(index);
    }

    getShaderExplorerFilename(index) {
        const slug = this.getShaderDisplayName(index).toLowerCase().replace(/[\s.-]+/g, '_').replace(/_+/g, '_');
        return `${formatTargetIndex(index)}_${slug}.wgsl`;
    }

    renderShaderExplorer() {
        if (!this.shaderExplorerElement) return;
        this.shaderExplorerElement.innerHTML = '';
        for (let index = 0; index < SettingsPanel.VIEW_TARGET_COUNT; index++) {
            const item = document.createElement('div');
            item.className = 'shader-explorer-item';
            if (index === this.shaderTargetIndex) {
                item.classList.add('selected');
            }
            if (isShaderEditingLocked(index)) {
                item.classList.add('locked');
            }
            if (this.shaderRenamingIndex === index) {
                item.classList.add('renaming');
                const prefix = document.createElement('span');
                prefix.className = 'shader-explorer-prefix';
                prefix.textContent = `${formatTargetIndex(index)}_`;
                const input = document.createElement('input');
                input.type = 'text';
                input.className = 'shader-explorer-rename-input';
                input.dataset.index = String(index);
                input.spellcheck = false;
                input.value = this.getShaderDisplayName(index);
                const suffix = document.createElement('span');
                suffix.className = 'shader-explorer-suffix';
                suffix.textContent = '.wgsl';
                input.addEventListener('keydown', (event) => {
                    event.stopPropagation();
                    if (event.key === 'Enter') {
                        event.preventDefault();
                        this.commitRenameShader(index, input.value);
                    } else if (event.key === 'Escape') {
                        event.preventDefault();
                        this.cancelRenameShader();
                    }
                });
                input.addEventListener('keypress', (event) => event.stopPropagation());
                input.addEventListener('input', (event) => event.stopPropagation());
                input.addEventListener('mousedown', (event) => event.stopPropagation());
                input.addEventListener('click', (event) => event.stopPropagation());
                item.appendChild(prefix);
                item.appendChild(input);
                item.appendChild(suffix);
                setTimeout(() => {
                    input.focus();
                    input.select();
                }, 0);
            } else {
                const selectBtn = document.createElement('button');
                selectBtn.type = 'button';
                selectBtn.className = 'shader-explorer-select';
                selectBtn.textContent = this.getShaderExplorerFilename(index);
                selectBtn.addEventListener('click', () => {
                    this.loadShaderEditorForTarget(index);
                });

                item.appendChild(selectBtn);

                if (!isShaderEditingLocked(index)) {
                    const renameBtn = document.createElement('button');
                    renameBtn.type = 'button';
                    renameBtn.className = 'shader-explorer-rename';
                    renameBtn.title = 'Rename display label';
                    renameBtn.innerHTML = `
                        <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                            <path d="M12 20h9"></path>
                            <path d="M16.5 3.5a2.121 2.121 0 1 1 3 3L7 19l-4 1 1-4Z"></path>
                        </svg>
                    `;
                    renameBtn.addEventListener('click', (event) => {
                        event.stopPropagation();
                        this.startRenameShader(index);
                    });
                    item.appendChild(renameBtn);
                }
            }
            this.shaderExplorerElement.appendChild(item);
        }
    }

    startRenameShader(index) {
        if (isShaderEditingLocked(index)) return;
        this.shaderRenamingIndex = index;
        this.renderShaderExplorer();
    }

    commitRenameShader(index, rawValue) {
        const trimmed = rawValue.trim();
        if (trimmed) {
            this.shaderDisplayNameDrafts[index] = trimmed.toUpperCase().replace(/[\s.-]+/g, '_').replace(/_+/g, '_');
        }
        this.shaderRenamingIndex = null;
        this.renderShaderExplorer();
    }

    cancelRenameShader() {
        this.shaderRenamingIndex = null;
        this.renderShaderExplorer();
    }

    initializeShaderDrafts() {
        this.shaderDrafts = {};
        this.baselineShaderDrafts = {};
        for (let index = 0; index < SettingsPanel.VIEW_TARGET_COUNT; index++) {
            const source = this.readShaderSourceFromWasm(index);
            this.shaderDrafts[index] = source;
            this.baselineShaderDrafts[index] = source;
        }
    }

    flushShaderEditorDraft() {
        if (!this.shaderEditorElement || this.shaderEditorElement.readOnly) return;
        if (isShaderEditingLocked(this.shaderTargetIndex)) return;
        this.shaderDrafts[this.shaderTargetIndex] = this.shaderEditorElement.value;
    }

    updateShaderEditorChrome() {
        const editing = this.shaderEditorEditing;
        if (this.shaderEditorEditButton) {
            this.shaderEditorEditButton.classList.toggle('hidden', editing);
            if (isShaderEditingLocked(this.shaderTargetIndex)) {
                this.shaderEditorEditButton.disabled = true;
                this.shaderEditorEditButton.classList.add('input-mode-btn-disabled');
            }
        }
        if (this.shaderEditorSaveButton) {
            this.shaderEditorSaveButton.classList.toggle('hidden', !editing);
        }
        if (this.shaderEditorCancelButton) {
            this.shaderEditorCancelButton.classList.toggle('hidden', !editing);
        }
        if (this.shaderEditorShellElement) {
            this.shaderEditorShellElement.classList.toggle('editing', editing);
        }
        if (this.shaderEditorElement) {
            this.shaderEditorElement.readOnly = !editing;
            if (editing) {
                this.shaderEditorElement.removeAttribute('readonly');
            } else {
                this.shaderEditorElement.setAttribute('readonly', '');
            }
        }
    }

    bindShaderEditorInteractionEvents() {
        if (!this.shaderEditorElement) return;

        const stopWhenEditing = (event) => {
            if (this.shaderEditorEditing) {
                event.stopPropagation();
            }
        };

        for (const eventName of ['keydown', 'keypress', 'keyup', 'input', 'beforeinput', 'mousedown', 'click', 'paste']) {
            this.shaderEditorElement.addEventListener(eventName, stopWhenEditing);
        }

        this.shaderEditorElement.addEventListener('mousedown', () => {
            if (this.shaderEditorEditing) {
                this.shaderEditorElement.focus();
            }
        });
    }

    focusShaderEditor() {
        if (!this.shaderEditorElement || !this.shaderEditorEditing) return;
        this.shaderEditorElement.focus();
        const length = this.shaderEditorElement.value.length;
        this.shaderEditorElement.setSelectionRange(length, length);
    }

    startShaderEditorEdit() {
        if (!this.shaderEditorElement || isShaderEditingLocked(this.shaderTargetIndex)) return;
        this.shaderEditorSessionDraft = this.shaderEditorElement.value;
        this.shaderEditorEditing = true;
        this.updateShaderEditorChrome();
        requestAnimationFrame(() => this.focusShaderEditor());
    }

    saveShaderEditorEdit() {
        if (!this.shaderEditorEditing) return;
        this.shaderDrafts[this.shaderTargetIndex] = this.shaderEditorElement.value;
        this.shaderEditorEditing = false;
        this.shaderEditorSessionDraft = '';
        this.updateShaderEditorChrome();
    }

    cancelShaderEditorEdit() {
        if (!this.shaderEditorEditing || !this.shaderEditorElement) return;
        this.shaderEditorElement.value = this.shaderEditorSessionDraft;
        this.shaderEditorEditing = false;
        this.shaderEditorSessionDraft = '';
        this.updateShaderEditorChrome();
    }

    loadShaderEditorForTarget(index) {
        if (this.shaderEditorEditing) {
            this.cancelShaderEditorEdit();
        }
        if (this.shaderRenamingIndex === index && isShaderEditingLocked(index)) {
            this.shaderRenamingIndex = null;
        }
        this.shaderTargetIndex = index;
        if (this.shaderEditorElement) {
            this.shaderEditorElement.value = this.shaderDrafts[index] ?? this.readShaderSourceFromWasm(index);
        }
        this.shaderEditorEditing = false;
        this.shaderEditorSessionDraft = '';
        this.updateShaderEditorChrome();
        this.renderShaderExplorer();
        this.hideShaderError();
    }

    switchTab(tab) {
        if (tab === this.activeTab) return;
        if (this.activeTab === 'shader') {
            if (this.shaderEditorEditing) {
                this.cancelShaderEditorEdit();
            } else {
                this.flushShaderEditorDraft();
            }
        }
        this.activeTab = tab;
        this.panelElement?.querySelectorAll('.settings-tab').forEach((button) => {
            button.classList.toggle('selected', button.dataset.tab === tab);
        });
        if (this.settingsTabPane) {
            this.settingsTabPane.classList.toggle('hidden', tab !== 'settings');
        }
        if (this.shaderTabPane) {
            this.shaderTabPane.classList.toggle('hidden', tab !== 'shader');
        }
        if (tab === 'shader') {
            this.loadShaderEditorForTarget(this.shaderTargetIndex);
        }
    }

    showShaderError(message) {
        this.shaderError = message;
        if (!this.shaderErrorElement) return;
        this.shaderErrorElement.textContent = message;
        this.shaderErrorElement.classList.remove('hidden');
    }

    hideShaderError() {
        this.shaderError = null;
        this.shaderErrorElement?.classList.add('hidden');
    }

    shaderDraftsChanged() {
        for (let index = 0; index < SettingsPanel.VIEW_TARGET_COUNT; index++) {
            if (isShaderEditingLocked(index)) continue;
            if ((this.shaderDrafts[index] ?? '') !== (this.baselineShaderDrafts[index] ?? '')) {
                return true;
            }
        }
        return false;
    }

    displayNamesChanged() {
        for (let index = 0; index < SettingsPanel.VIEW_TARGET_COUNT; index++) {
            if (isShaderEditingLocked(index)) continue;
            if ((this.shaderDisplayNameDrafts[index] ?? '') !== (this.baselineShaderDisplayNames[index] ?? '')) {
                return true;
            }
        }
        return false;
    }

    getShaderTargetLabel(index) {
        return this.getShaderExplorerFilename(index);
    }

    ensurePersistViewsDir(FS) {
        const dir = '/persist/views';
        try {
            FS.mkdir('/persist');
        } catch (_) { /* exists */ }
        try {
            FS.mkdir(dir);
        } catch (_) { /* exists */ }
    }

    getShaderErrorMessage(mod, fallback) {
        const errorPtr = mod._getLastShaderError?.();
        if (!errorPtr) {
            return fallback;
        }
        const message = mod.UTF8ToString(errorPtr);
        mod._freeShaderString(errorPtr);
        return message || fallback;
    }

    async persistShaderDrafts() {
        const mod = this.parentApp.module;
        const FS = mod?.FS;
        if (!FS || !mod._setViewSource) {
            return true;
        }

        this.ensurePersistViewsDir(FS);

        for (let index = 0; index < SettingsPanel.VIEW_TARGET_COUNT; index++) {
            if (isShaderEditingLocked(index)) continue;
            const draft = this.shaderDrafts[index] ?? '';
            const baseline = this.baselineShaderDrafts[index] ?? '';
            const persistPath = `/persist/views/${index}.wgsl`;

            if (draft !== baseline) {
                FS.writeFile(persistPath, draft);
                if (!mod._setViewSource(index, draft)) {
                    this.showShaderError(this.getShaderErrorMessage(mod, `Invalid shader for target ${index}`));
                    return false;
                }
            } else {
                try {
                    if (FS.analyzePath(persistPath).exists) {
                        FS.unlink(persistPath);
                    }
                } catch (_) { /* ignore */ }
                if (mod._resetViewSource) {
                    mod._resetViewSource(index);
                }
            }
        }

        if (!mod._applyViewShaders || mod._applyViewShaders() !== 0) {
            this.showShaderError(this.getShaderErrorMessage(mod, 'Shader compilation failed'));
            return false;
        }

        for (let index = 0; index < SettingsPanel.VIEW_TARGET_COUNT; index++) {
            this.baselineShaderDrafts[index] = this.shaderDrafts[index] ?? '';
        }
        for (let index = 0; index < SettingsPanel.VIEW_TARGET_COUNT; index++) {
            this.baselineShaderDisplayNames[index] = this.shaderDisplayNameDrafts[index] ?? getBuiltinShaderDisplayName(index);
        }
        return true;
    }

    mergeCustomViewsIntoConfig(config) {
        if (!config.customViews) {
            config.customViews = {};
        }
        for (let index = 0; index < SettingsPanel.VIEW_TARGET_COUNT; index++) {
            if (isShaderEditingLocked(index)) {
                delete config.customViews?.[String(index)];
                continue;
            }
            const key = String(index);
            const draft = this.shaderDrafts[index] ?? '';
            const baseline = this.baselineShaderDrafts[index] ?? '';
            const displayName = this.getShaderDisplayName(index);
            const builtinName = getBuiltinShaderDisplayName(index);
            const entry = { ...(config.customViews[key] ?? {}) };

            if (draft !== baseline) {
                entry.overridden = true;
            } else {
                delete entry.overridden;
            }

            if (displayName !== builtinName) {
                entry.displayName = displayName;
            } else {
                delete entry.displayName;
            }

            if (Object.keys(entry).length === 0) {
                delete config.customViews[key];
            } else {
                config.customViews[key] = entry;
            }
        }
        if (Object.keys(config.customViews).length === 0) {
            delete config.customViews;
        }
    }

    addSection(section) {
        this.sections.push(section);
        let resolutionControl = null;
        let simModeControl = null;
        section.getAllControls().forEach((control) => {
            if (control instanceof SliderControl && control.configPath === 'simulation.resolution') {
                resolutionControl = control;
                control.liveChange = (value) => this.onResolutionPreview(value);
            }
            if (control instanceof SimModeControl) {
                simModeControl = control;
            }
        });
        if (simModeControl && resolutionControl) {
            simModeControl.linkResolutionControl(
                resolutionControl,
                (value) => this.onResolutionPreview(value)
            );
        }
    }

    onResolutionPreview(value) {
        if (!window.kataraConfig) window.kataraConfig = {};
        if (!window.kataraConfig.simulation) window.kataraConfig.simulation = {};
        window.kataraConfig.simulation.resolution = value;
        this.refreshScaleControls();
    }

    refreshScaleControls() {
        this.sections.forEach((section) => {
            section.getAllControls().forEach((control) => {
                if (control instanceof VorticityControl || control instanceof CircleControl) {
                    control.draw();
                }
            });
        });
    }

    async open() {
        if (this.isOpen) return;
        this.wasPausedBeforeOpen = this.parentApp.simulationPaused;
        if (!this.wasPausedBeforeOpen) {
            this.parentApp.togglePause();
        }
        this.saveOriginalConfig();
        this.initializeShaderDrafts();
        this.initializeShaderDisplayNames();
        this.createPanel();
        this.loadCurrentValues();
        const pipeline = this.originalConfig?.pipeline ?? 'device';
        const resolution = this.originalConfig?.simulation?.resolution
            ?? defaultResolutionForPipeline(pipeline);
        this.onResolutionPreview(resolution);
        await this.refreshEnvironmentControls();
        this.boundKeyHandler = this.handleKeyDown.bind(this);
        this.attachKeyHandler();
        this.isOpen = true;
    }

    attachKeyHandler() {
        if (!this.panelElement || !this.boundKeyHandler) return;
        this.panelElement.addEventListener('keydown', this.boundKeyHandler);
    }

    detachKeyHandler() {
        if (!this.panelElement || !this.boundKeyHandler) return;
        this.panelElement.removeEventListener('keydown', this.boundKeyHandler);
    }

    async refreshEnvironmentControls() {
        if (this.parentApp?.updateInkAspectRatioFromConfig) {
            await this.parentApp.updateInkAspectRatioFromConfig();
        }
        const refresh = () => {
            this.sections.forEach((section) => {
                section.getAllControls().forEach((control) => {
                    if (control instanceof EnvironmentControl || control instanceof VorticityControl) {
                        control.refreshLayout();
                    }
                });
            });
            this.refreshScaleControls();
        };
        refresh();
        requestAnimationFrame(refresh);
    }

    async close(saveChanges = false) {
        if (!this.isOpen) return;

        this.detachKeyHandler();

        if (saveChanges) {
            const result = await this.saveAndApply();
            if (result === 'shader') {
                this.switchTab('shader');
                this.attachKeyHandler();
                return;
            }
            if (result === 'error') {
                this.attachKeyHandler();
                return;
            }
        } else {
            if (this.shaderEditorEditing) {
                this.cancelShaderEditorEdit();
            }
            this.shaderDrafts = { ...this.baselineShaderDrafts };
            this.shaderDisplayNameDrafts = { ...this.baselineShaderDisplayNames };
            this.shaderRenamingIndex = null;
        }

        this.panelElement?.remove();
        this.boundKeyHandler = null;

        if (!this.wasPausedBeforeOpen && this.parentApp.simulationPaused) {
            this.parentApp.togglePause();
        }

        this.isOpen = false;
    }

    createPanel() {
        const panel = document.createElement('div');
        panel.className = 'settings-panel';
        panel.innerHTML = `
            <div class="settings-panel-header">
                <div class="settings-panel-tabs">
                    <button type="button" class="input-mode-btn settings-tab selected" data-tab="settings">SETTINGS</button>
                    <button type="button" class="input-mode-btn settings-tab settings-tab-shader" data-tab="shader">SHADERS</button>
                </div>
                <div class="settings-panel-buttons">
                    <button class="input-mode-btn settings-btn-cancel" title="Discard (ESC)">
                        <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                            <line x1="18" y1="6" x2="6" y2="18"></line>
                            <line x1="6" y1="6" x2="18" y2="18"></line>
                        </svg>
                        <span>[ESC]</span>
                    </button>
                    <button class="input-mode-btn settings-btn-save" title="Save (Enter)">
                        <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                            <polyline points="20,6 9,17 4,12"></polyline>
                        </svg>
                        <span>[ENTER]</span>
                    </button>
                </div>
            </div>
            <div class="settings-panel-body">
                <div class="settings-panel-content settings-tab-pane" data-tab="settings"></div>
                <div class="settings-panel-content settings-tab-pane hidden shader-editor-pane" data-tab="shader">
                    <div class="shader-workspace">
                        <div class="shader-explorer"></div>
                        <div class="shader-editor-column">
                            <div class="shader-editor-toolbar">
                                <button type="button" class="input-mode-btn input-mode-btn-disabled shader-editor-edit" disabled>EDIT</button>
                                <button type="button" class="input-mode-btn shader-editor-save hidden">SAVE</button>
                                <button type="button" class="input-mode-btn shader-editor-cancel hidden">CANCEL</button>
                            </div>
                            <div class="shader-error-box hidden"></div>
                            <div class="shader-editor-shell">
                                <textarea class="shader-editor" spellcheck="false" readonly></textarea>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        `;
        panel.querySelector('.settings-btn-cancel').addEventListener('click', () => this.close(false));
        panel.querySelector('.settings-btn-save').addEventListener('click', () => this.close(true));
        panel.querySelectorAll('.settings-tab').forEach((button) => {
            button.addEventListener('click', () => this.switchTab(button.dataset.tab));
        });
        panel.tabIndex = -1;
        setTimeout(() => panel.focus(), 0);

        this.settingsTabPane = panel.querySelector('.settings-tab-pane[data-tab="settings"]');
        this.shaderTabPane = panel.querySelector('.settings-tab-pane[data-tab="shader"]');
        this.shaderExplorerElement = panel.querySelector('.shader-explorer');
        this.shaderEditorToolbarElement = panel.querySelector('.shader-editor-toolbar');
        this.shaderEditorEditButton = panel.querySelector('.shader-editor-edit');
        this.shaderEditorSaveButton = panel.querySelector('.shader-editor-save');
        this.shaderEditorCancelButton = panel.querySelector('.shader-editor-cancel');
        this.shaderEditorShellElement = panel.querySelector('.shader-editor-shell');
        this.shaderErrorElement = panel.querySelector('.shader-error-box');
        this.shaderEditorElement = panel.querySelector('.shader-editor');
        this.shaderEditorEditButton.addEventListener('click', () => this.startShaderEditorEdit());
        this.shaderEditorSaveButton.addEventListener('click', () => this.saveShaderEditorEdit());
        this.shaderEditorCancelButton.addEventListener('click', () => this.cancelShaderEditorEdit());
        this.bindShaderEditorInteractionEvents();
        this.shaderErrorElement.addEventListener('click', () => this.hideShaderError());
        this.updateShaderEditorChrome();
        this.renderShaderExplorer();

        const content = this.settingsTabPane;
        const cols = document.createElement('div');
        cols.className = 'settings-columns';
        cols.innerHTML = `
            <div class="settings-col settings-col-left"></div>
            <div class="settings-col settings-col-right"></div>
        `;
        const leftCol = cols.querySelector('.settings-col-left');
        const rightCol = cols.querySelector('.settings-col-right');
        const rightTop = document.createElement('div');
        rightTop.className = 'settings-col-right-top';
        const rightBottom = document.createElement('div');
        rightBottom.className = 'settings-col-right-bottom';
        rightCol.appendChild(rightTop);
        rightCol.appendChild(rightBottom);
        this.sections.forEach(section => {
            if (section.column === 'right') {
                (section.placement === 'bottom' ? rightBottom : rightTop).appendChild(section.render());
            } else {
                leftCol.appendChild(section.render());
            }
        });
        content.appendChild(cols);

        this.loadShaderEditorForTarget(this.shaderTargetIndex);

        this.panelElement = panel;
        document.body.appendChild(panel);
    }

    handleKeyDown(e) {
        if (!this.isOpen) return;

        const inRenameInput = e.target instanceof HTMLInputElement &&
            e.target.classList.contains('shader-explorer-rename-input');
        const inShaderEditor = e.target instanceof HTMLTextAreaElement &&
            e.target.classList.contains('shader-editor');

        if (inRenameInput) {
            return;
        }

        if (inShaderEditor && this.shaderEditorEditing) {
            if (e.key === 'Escape') {
                e.preventDefault();
                e.stopPropagation();
                this.cancelShaderEditorEdit();
            }
            return;
        }

        if (this.shaderRenamingIndex !== null && e.key === 'Escape') {
            e.preventDefault();
            e.stopPropagation();
            this.cancelRenameShader();
            return;
        }

        if (e.key === 'Escape') {
            e.preventDefault();
            e.stopPropagation();
            void this.close(false);
            return;
        }

        if (e.key === 'Enter') {
            e.preventDefault();
            e.stopPropagation();
            void this.close(true);
        }
    }

    loadCurrentValues() {
        if (!this.originalConfig) return;
        const config = this.originalConfig;
        const pipeline = config.pipeline ?? 'device';
        let simModeControl = null;
        this.sections.forEach(section => {
            section.getAllControls().forEach(control => {
                if (control instanceof SimModeControl) {
                    simModeControl = control;
                    return;
                }
                loadControlValue(control, config);
            });
        });
        if (simModeControl) {
            simModeControl.loadValue(pipeline);
            simModeControl.applyPipelineUi(pipeline);
        }
    }

    async refresh() {
        if (!this.isOpen) return;
        this.saveOriginalConfig();
        this.loadCurrentValues();
        await this.refreshEnvironmentControls();
    }

    saveOriginalConfig() {
        if (!this.parentApp.module?.FS) return;
        try {
            const configText = this.parentApp.module.FS.readFile('/config.json', { encoding: 'utf8' });
            this.originalConfig = JSON.parse(configText);
        } catch (err) {
            console.error('Failed to save original config:', err);
            this.originalConfig = null;
        }
    }

    collectAllValues() {
        const config = {};
        this.sections.forEach(section => deepMerge(config, section.collectValues()));
        return config;
    }

    async saveAndApply() {
        if (this.shaderEditorEditing) {
            this.saveShaderEditorEdit();
        }
        this.flushShaderEditorDraft();

        const mergedConfig = deepMerge(deepMerge({}, this.originalConfig || {}), this.collectAllValues());
        const pipeline = mergedConfig.pipeline ?? 'device';
        mergedConfig.simulation = mergedConfig.simulation ?? {};
        mergedConfig.simulation.resolution = clampResolutionForPipeline(
            mergedConfig.simulation.resolution ?? defaultResolutionForPipeline(pipeline),
            pipeline
        );
        mergedConfig.simulation.projection = mergedConfig.simulation.projection ?? {};
        mergedConfig.simulation.projection.iterations = clampIterationsForPipeline(
            mergedConfig.simulation.projection.iterations ?? defaultIterationsForPipeline(pipeline),
            pipeline
        );
        const sectionChanged = (key) =>
            JSON.stringify(this.originalConfig?.[key] ?? {}) !== JSON.stringify(mergedConfig?.[key] ?? {});
        const layoutChanged = sectionChanged('layout');
        const simulationChanged = sectionChanged('simulation');
        const pipelineChanged =
            (this.originalConfig?.pipeline ?? 'device') !== (mergedConfig.pipeline ?? 'device');
        try {
            this.parentApp.mergeViewportTargetsIntoConfig(mergedConfig);
            this.mergeCustomViewsIntoConfig(mergedConfig);
            const configText = JSON.stringify(mergedConfig, null, 4);
            this.parentApp.module.FS.writeFile('/config.json', configText);

            if (this.parentApp.idbfsAvailable) {
                this.parentApp.module.FS.writeFile('/persist/config.json', configText);
            }

            window.kataraConfig = mergedConfig;

            if (pipelineChanged) {
                if (this.parentApp.idbfsAvailable) {
                    await new Promise((resolve) => {
                        this.parentApp.module.FS.syncfs(false, (err) => {
                            if (err) console.error('syncfs failed:', err);
                            resolve();
                        });
                    });
                }
                localStorage.setItem('katara_needs_restore', 'true');
                window.location.reload();
                return 'reload';
            }

            if (this.parentApp.setInputMode) {
                this.parentApp.setInputMode(mergedConfig.inputMode ?? 'hand', { persist: false });
            }

            if (this.parentApp.module?._reloadConfig) {
                try {
                    let flags = 0;
                    if (simulationChanged) flags |= 2;
                    if (layoutChanged) flags |= (4 | 8);
                    if (flags !== 0) {
                        this.parentApp.module._reloadConfig(flags);
                    }
                } catch (e) {
                    console.error('reloadConfig error:', e);
                }
            }

            if (this.shaderDraftsChanged()) {
                const shadersOk = await this.persistShaderDrafts();
                if (!shadersOk) {
                    return 'shader';
                }
            }

            if (this.parentApp.idbfsAvailable) {
                await new Promise((resolve) => {
                    this.parentApp.module.FS.syncfs(false, (err) => {
                        if (err) console.error('syncfs failed:', err);
                        resolve();
                    });
                });
                localStorage.setItem('katara_needs_restore', 'true');
            }

            this.parentApp.syncLayoutStateFromConfig();
            this.parentApp.updateCameraUi();
            if (layoutChanged) {
                this.parentApp.refreshSimLayout();
            } else {
                this.parentApp.updatePlotLabels();
                this.parentApp.updateViewportButtons();
            }
            return 'ok';
        } catch (err) {
            console.error('Failed to save config:', err);
            this.showShaderError(err?.message || 'Failed to save settings');
            return 'error';
        }
    }
}

function createSkeletonSections(currentConfig, options = {}) {
    const sections = [];
    const sim = currentConfig.simulation ?? {};
    const proj = sim.projection ?? {};
    const vorticity = sim.vorticity ?? {};
    const layout = currentConfig.layout ?? {};
    const cameraDetected = options.cameraDetected === true;

    const pipeline = currentConfig.pipeline ?? 'device';
    const resolutionRange = resolutionRangeForPipeline(pipeline);
    const initialResolution = clampResolutionForPipeline(
        sim.resolution ?? defaultResolutionForPipeline(pipeline),
        pipeline
    );

    const simSection = new ConfigSection('Simulation', { layout: 'grid2' });
    const simModeControl = new SimModeControl(pipeline);
    simSection.addHeaderControl(simModeControl);
    const resolutionControl = new SliderControl(
        'simulation.resolution',
        'Resolution',
        initialResolution,
        resolutionRange.min,
        resolutionRange.max,
        resolutionRange.step
    );
    simSection.addControl(resolutionControl);
    simSection.addControl(new SliderControl('simulation.timestep', 'Timestep', sim.timestep ?? 0.02, 0.001, 0.1, 0.001));
    sections.push(simSection);

    const projSection = new ConfigSection('Pressure Solver', { layout: 'grid2' });
    const overrelaxationControl = new SliderControl(
        'simulation.projection.overrelaxationCoefficient',
        'Overrelaxation',
        proj.overrelaxationCoefficient ?? 1.95,
        1.0,
        1.95,
        0.05
    );
    if (isGpuSimulatorMode(currentConfig)) {
        overrelaxationControl.setDisabled(true);
    }
    const iterationRange = iterationRangeForPipeline(pipeline);
    const initialIterations = clampIterationsForPipeline(
        proj.iterations ?? defaultIterationsForPipeline(pipeline),
        pipeline
    );
    const iterationsControl = new SliderControl(
        'simulation.projection.iterations',
        'Iterations',
        initialIterations,
        iterationRange.min,
        iterationRange.max,
        iterationRange.step
    );
    simModeControl.linkOverrelaxationControl(overrelaxationControl);
    simModeControl.linkIterationsControl(iterationsControl);
    projSection.addControl(overrelaxationControl);
    projSection.addControl(iterationsControl);
    sections.push(projSection);

    const layoutSection = new ConfigSection('Layout');
    layoutSection.addControl(new LayoutPresetControl(
        'layout.preset',
        'Layout Preset',
        layout.preset ?? 'default'
    ));
    layoutSection.addHeaderControl(new HeaderCheckboxControl(
        'layout.labelsEnabled',
        'Labels',
        layout.labelsEnabled ?? true
    ));
    layoutSection.addHeaderControl(new HeaderCheckboxControl(
        'layout.buttonsEnabled',
        'Buttons',
        layout.buttonsEnabled ?? true
    ));
    const cameraCheckbox = new HeaderCheckboxControl(
        'layout.camerasEnabled',
        'Camera',
        cameraDetected ? (layout.camerasEnabled ?? true) : false
    );
    if (!cameraDetected) {
        cameraCheckbox.collectWhenDisabled = true;
        cameraCheckbox.setDisabled(true);
    }
    layoutSection.addHeaderControl(cameraCheckbox);
    sections.push(layoutSection);

    const plotsSection = new ConfigSection('Plots', { layout: 'grid4' });
    plotsSection.addControl(new CheckboxControl('layout.components.density_histogram.histogramEnabled', 'Density', currentConfig.layout?.components?.density_histogram?.histogramEnabled ?? true));
    plotsSection.addControl(new CheckboxControl('layout.components.velocity_histogram.histogramEnabled', 'Velocity', currentConfig.layout?.components?.velocity_histogram?.histogramEnabled ?? true));
    plotsSection.addControl(new CheckboxControl('layout.components.entropy_time_series.histogramEnabled', 'Entropy', currentConfig.layout?.components?.entropy_time_series?.histogramEnabled ?? true));
    plotsSection.addControl(new CheckboxControl('layout.components.volume_time_series.histogramEnabled', 'Volume', currentConfig.layout?.components?.volume_time_series?.histogramEnabled ?? false));
    sections.push(plotsSection);

    const interactionSection = new ConfigSection('Interaction', {
        column: 'right',
        placement: 'top',
        layout: 'canvasRow'
    });
    const inputModeControl = new InputModeControl(
        'inputMode',
        cameraDetected ? (currentConfig.inputMode ?? 'hand') : 'mouse_pull'
    );
    interactionSection.addHeaderControl(inputModeControl);
    const circle = currentConfig.simulation?.circle ?? {};
    const circleControl = new CircleControl(
        'simulation.circle.radius',
        'simulation.circle.momentumTransferRadius',
        'simulation.circle.momentumTransferStrength',
        circle.radius ?? 0.02,
        Math.max(1.0, Math.min(2.0, circle.momentumTransferRadius ?? 2.0)),
        circle.momentumTransferStrength ?? 5.0
    );
    const hands = currentConfig.hands ?? {};
    const handControl = new HandControl(
        'hands.left',
        'hands.right',
        'simulation.circle.handSensitivity',
        cameraDetected ? (hands.left ?? 'full') : 'none',
        cameraDetected ? (hands.right ?? 'full') : 'none',
        circle.handSensitivity ?? 0.3
    );
    handControl.linkCircleControl(circleControl);
    inputModeControl.linkHandControl(handControl, cameraDetected);
    inputModeControl.linkCircleControl(circleControl);
    if (!cameraDetected) {
        handControl.collectWhenDisabled = true;
        handControl.setDisabled(true);
    }
    interactionSection.addControl(circleControl);
    interactionSection.addControl(handControl);
    sections.push(interactionSection);

    const windTunnel = sim.windTunnel ?? {};
    const envSection = new ConfigSection('Environment', { column: 'right', placement: 'bottom' });
    const envControl = new EnvironmentControl(sim.edges ?? 14, windTunnel);
    envSection.addControl(envControl);
    sections.push(envSection);

    const vortSection = new ConfigSection('Vorticity', { column: 'right', placement: 'bottom' });
    const vortControl = new VorticityControl(
        'simulation.vorticity.strength',
        'simulation.vorticity.lengthScale',
        vorticity.strength ?? 20,
        vorticity.lengthScale ?? 10,
        30, // max strength
        20  // max impact
    );
    vortSection.addControl(vortControl);
    sections.push(vortSection);

    return sections;
}

export {
    SettingsPanel,
    createSkeletonSections,
    formatTargetIndex,
    getShaderTargetName,
    measureElementWidth,
    measureElementHeight,
    getSimViewportSize,
    HAND_CONNECTIONS,
    ACTIVE_LANDMARKS_BY_MODE,
    HAND_COLORS,
    Palette,
    ControlPalette,
};
