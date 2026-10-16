import { HandPalette, Palette } from '../palette.js';
import { readCommittedConfig } from '../config.js';

// --- reload flags ---

export const RELOAD_FLAGS = {
    INK: 1,
    SIM: 2,
    RENDER: 4,
    LAYOUT: 8,
};

export function reloadFlagsForSectionChanges({ simulationChanged, layoutChanged }) {
    let flags = 0;
    if (simulationChanged) flags |= RELOAD_FLAGS.SIM;
    if (layoutChanged) flags |= RELOAD_FLAGS.RENDER | RELOAD_FLAGS.LAYOUT;
    return flags;
}

// --- config patch ---

function isPlainObject(value) {
    return !!value && typeof value === 'object' && !Array.isArray(value);
}

function assignSection(target, section) {
    if (!isPlainObject(section)) return;
    for (const [key, value] of Object.entries(section)) {
        if (isPlainObject(value) && isPlainObject(target[key])) {
            assignSection(target[key], value);
        } else {
            target[key] = value;
        }
    }
}

export function assembleConfigFromSections(originalConfig, sectionPatches) {
    const merged = structuredClone(originalConfig ?? {});
    for (const patch of sectionPatches) {
        assignSection(merged, patch);
    }
    return merged;
}

export function collectSectionPatches(sections) {
    return sections.map((section) => section.collectValues());
}

// --- shared DOM / input helpers ---

function isTextEntryElement(el) {
    if (el instanceof HTMLTextAreaElement) return true;
    if (!(el instanceof HTMLInputElement)) return false;
    const type = el.type.toLowerCase();
    return type === 'text' || type === 'number' || type === 'search'
        || type === 'password' || type === 'email' || type === 'url';
}

function makeMouseOnlyControl(el) {
    if (el.dataset.kataraMouseOnly) return;
    el.dataset.kataraMouseOnly = '1';
    el.tabIndex = -1;
    el.addEventListener('keydown', (e) => {
        e.preventDefault();
    });
}

export function disablePanelKeyboardFocus(root) {
    if (!root) return;
    root.querySelectorAll('button, input[type="range"], input[type="checkbox"], select').forEach(makeMouseOnlyControl);
}

export { isTextEntryElement };

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

export function measureElementWidth(el) {
    return measureHiddenElement(el, 'width');
}

export function measureElementHeight(el) {
    return measureHiddenElement(el, 'height');
}

export function getSimViewportSize() {
    const viewport = document.querySelector('.sim-viewport');
    if (!viewport) {
        throw new Error('Sim viewport not found');
    }
    return {
        width: viewport.clientWidth,
        height: viewport.clientHeight,
    };
}

// Matches styles.css settings mobile breakpoint.
export const MOBILE_LAYOUT_MAX_WIDTH_PX = 900;

const LAYOUT_PLOT_KEYS = [
    'density_histogram',
    'velocity_histogram',
    'entropy_time_series',
    'volume_time_series',
];

export function isMobileViewport() {
    return typeof window !== 'undefined'
        && !!window.matchMedia
        && window.matchMedia(`(max-width: ${MOBILE_LAYOUT_MAX_WIDTH_PX}px)`).matches;
}

function setPlotEnabled(components, name, enabled) {
    if (!components[name] || typeof components[name] !== 'object') {
        components[name] = {};
    }
    components[name].histogramEnabled = enabled;
}

/**
 * Gate default layout fields by viewport + camera availability:
 * - mobile: gallery_single, plots off
 * - desktop + camera: default dashboard, 3 plots, camera on
 * - desktop, no camera: gallery_quad, all plots, camera off
 */
export function applyDefaultLayoutGates(config, {
    isMobile = isMobileViewport(),
    cameraDetected = false,
} = {}) {
    if (!config || typeof config !== 'object') {
        return config;
    }

    if (!config.render || typeof config.render !== 'object') {
        config.render = {};
    }
    const render = config.render;
    if (!render.components || typeof render.components !== 'object') {
        render.components = {};
    }
    const components = render.components;

    if (isMobile) {
        render.preset = 'gallery_single';
        render.camerasEnabled = !!cameraDetected;
        for (const name of LAYOUT_PLOT_KEYS) {
            setPlotEnabled(components, name, false);
        }
        return config;
    }

    if (cameraDetected) {
        render.preset = 'default';
        render.camerasEnabled = true;
        setPlotEnabled(components, 'density_histogram', true);
        setPlotEnabled(components, 'velocity_histogram', true);
        setPlotEnabled(components, 'entropy_time_series', true);
        setPlotEnabled(components, 'volume_time_series', false);
        return config;
    }

    render.preset = 'gallery_quad';
    render.camerasEnabled = false;
    for (const name of LAYOUT_PLOT_KEYS) {
        setPlotEnabled(components, name, true);
    }
    return config;
}

export function getNestedValue(obj, path) {
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

export function requireConfigValue(config, path) {
    const value = getNestedValue(config, path);
    if (value === undefined || value === null) {
        console.error(`config.json: missing required value "${path}"`);
    }
    return value;
}

export function requireConfigObject(config, path) {
    const value = getNestedValue(config, path);
    if (!value || typeof value !== 'object' || Array.isArray(value)) {
        console.error(`config.json: missing required object "${path}"`);
        return null;
    }
    return value;
}

export function validateConfigPaths(config, paths) {
    if (!config || typeof config !== 'object') {
        console.error('config.json: invalid root config');
        return false;
    }

    let valid = true;
    for (const path of paths) {
        const value = getNestedValue(config, path);
        if (value === undefined || value === null) {
            console.error(`config.json: missing required value "${path}"`);
            valid = false;
        }
    }
    return valid;
}

export function setNestedValue(obj, path, value) {
    const keys = path.split('.');
    let current = obj;
    for (let i = 0; i < keys.length - 1; i++) {
        if (!(keys[i] in current)) current[keys[i]] = {};
        current = current[keys[i]];
    }
    current[keys[keys.length - 1]] = value;
}

// --- hand tracking constants ---

export const HAND_MODES = ['full', 'joints', 'claw', 'pointer', 'pointer-tip', 'none'];

export const HAND_MODE_LABELS = {
    full: 'Full',
    joints: 'Joints',
    pointer: 'Pointer',
    'pointer-tip': 'Dot',
    claw: 'Claw',
    none: 'Off',
};

export const HAND_COLORS = HandPalette;

export const ACTIVE_LANDMARKS_BY_MODE = {
    full: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20],
    joints: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20],
    pointer: [5, 6, 7, 8],
    'pointer-tip': [8],
    claw: [0, 1, 2, 3, 4, 5, 6, 7, 8],
    none: [],
};

export const HAND_CONNECTIONS = [
    [0, 1], [1, 2], [2, 3], [3, 4],
    [0, 5], [5, 6], [6, 7], [7, 8],
    [0, 9], [9, 10], [10, 11], [11, 12],
    [0, 13], [13, 14], [14, 15], [15, 16],
    [0, 17], [17, 18], [18, 19], [19, 20],
    [5, 9], [9, 13], [13, 17],
];

export const HAND_LANDMARKS = [
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

const MOUSE_INPUT_MODES = new Set(['mouse_pull']);

export const INPUT_MODE_INT = {
    hand: 0,
    mouse_pull: 1,
};

export function isMouseInputMode(mode) {
    return MOUSE_INPUT_MODES.has(mode);
}

// --- canvas control helpers ---

export const ENVIRONMENT_GLOW_BLUR = 12;
export const CONTROL_ARROW_HOVER_EXPAND = 1.4;

const CONTROL_ARROW_HIT_THRESHOLD = 12;
const CONTROL_ARROW_HOVER_THRESHOLD = 14;
const CONTROL_ARROW_HOVER_LERP = 10;
const CONTROL_ARROW_GLOW_SNAP = 0.02;

export function setupResizableCanvas(canvas, onResize) {
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

export function getCanvasEventPos(canvas, e) {
    const canvasRect = canvas.getBoundingClientRect();
    const scaleX = canvas.width / canvasRect.width;
    const scaleY = canvas.height / canvasRect.height;
    return {
        x: (e.clientX - canvasRect.left) * scaleX,
        y: (e.clientY - canvasRect.top) * scaleY,
    };
}

export function isNearPoint(pos, tip, threshold = 15) {
    const dx = pos.x - tip.x;
    const dy = pos.y - tip.y;
    return dx * dx + dy * dy < threshold * threshold;
}

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

export function setupControlCanvasArrowInteraction(canvas, { getTips, getArrowKeys, onRedraw }) {
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
            if (!pinnedKey) updatePointer(null);
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

export function setupControlCanvasGlowInteraction(canvas, { getActiveKey, getKeys, onRedraw }) {
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

export function applyControlCanvasGlow(ctx, glowT, maxBlur = 20) {
    const t = Math.max(0, Math.min(1, glowT));
    if (t <= 0) return;
    ctx.shadowColor = Palette.whiteBright;
    ctx.shadowBlur = t * maxBlur;
}

// --- shader helpers ---

// Populated from embedded views/registry.json via wasm after module init.
let viewTargetCount = 0;
/** @type {Record<number, string> | null} */
let viewRegistryNames = null;

export function initViewRegistryFromModule(mod) {
    if (!mod?._getViewTargetCount || !mod?._getViewName || !mod?._freeShaderString) {
        return;
    }
    viewTargetCount = mod._getViewTargetCount();
    viewRegistryNames = {};
    for (let index = 0; index < viewTargetCount; index++) {
        const ptr = mod._getViewName(index);
        if (!ptr) {
            continue;
        }
        viewRegistryNames[index] = mod.UTF8ToString(ptr);
        mod._freeShaderString(ptr);
    }
}

export function getViewTargetCount() {
    return viewTargetCount;
}

// Keep in sync with CameraBackgroundMode in config.h
export const CAMERA_BACKGROUND_NONE = 0;
export const CAMERA_BACKGROUND_CURRENT = 1;
export const CAMERA_BACKGROUND_INVERTED = 2;
export const CAMERA_BACKGROUND_MODE_COUNT = 3;

export function parseCameraBackgroundMode(value) {
    if (value === true || value === 1 || value === 'current') return CAMERA_BACKGROUND_CURRENT;
    if (value === 2 || value === 'inverted') return CAMERA_BACKGROUND_INVERTED;
    return CAMERA_BACKGROUND_NONE;
}

export function cameraBackgroundModeName(mode) {
    switch (mode) {
        case CAMERA_BACKGROUND_CURRENT: return 'current';
        case CAMERA_BACKGROUND_INVERTED: return 'inverted';
        default: return 'none';
    }
}

// Must match views/registry.json index for INK (and C++ layoutHasInkViewport).
export const INK_VIEW_TARGET = 2;

export function formatTargetIndex(index) {
    return String(index).padStart(2, '0');
}

export function getBuiltinShaderDisplayName(index) {
    // Display label for the ink target; registry name may still be INK until rebuild.
    if (index === INK_VIEW_TARGET) {
        return 'USR_IMG';
    }
    if (viewRegistryNames?.[index]) {
        return viewRegistryNames[index];
    }
    return `TARGET_${index}`;
}

export function getShaderTargetName(index, _app) {
    const customName = readCommittedConfig()?.customViews?.[String(index)]?.displayName;
    if (customName) {
        return customName;
    }
    return getBuiltinShaderDisplayName(index);
}
