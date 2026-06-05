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

function drawControlArrow(ctx, from, tip, color, headDir) {
    const headSize = 8;

    ctx.strokeStyle = color;
    ctx.fillStyle = color;
    ctx.lineWidth = 2;

    ctx.beginPath();
    ctx.moveTo(from.x, from.y);
    ctx.lineTo(tip.x, tip.y);
    ctx.stroke();

    ctx.beginPath();
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
    ctx.closePath();
    ctx.fill();

    ctx.beginPath();
    ctx.arc(tip.x, tip.y, 5, 0, Math.PI * 2);
    ctx.fill();
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

const HAND_MODES = ['full', 'joints', 'pointer', 'pointer-tip', 'none'];

const HAND_MODE_LABELS = {
    full: 'Full',
    joints: 'Joints',
    pointer: 'Pointer',
    'pointer-tip': 'Dot',
    none: 'Off',
};

const HAND_COLORS = {
    left: '#00ff88',
    right: '#ff9933',
    grey: 'rgba(255, 255, 255, 0.18)',
    greyDot: 'rgba(255, 255, 255, 0.3)',
    overlayGrey: 'rgba(255, 255, 255, 0.2)',
};

const ACTIVE_LANDMARKS_BY_MODE = {
    full: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20],
    joints: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20],
    pointer: [5, 6, 7, 8],
    'pointer-tip': [8],
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

const ControlPalette = {
    spiral: 'rgba(255, 255, 255, 0.85)',
    circle: 'rgba(170, 170, 170, 0.95)',
    radius: 'rgba(0, 255, 136, 0.95)',
    impact: 'rgba(100, 181, 246, 0.95)',
    effect: 'rgba(255, 153, 51, 0.75)',
    effectRing: 'rgba(255, 153, 51, 0.22)',
};

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
        const configDisplay = document.querySelector('.vorticity-strength-value');
        if (configDisplay) {
            return window.kataraConfig?.simulation?.resolution || 400;
        }
        return 400;
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
    const stroke = 'rgba(255,255,255,0.55)';
    const viewport = 'rgba(100,181,246,0.35)';
    const plot = 'rgba(129,199,132,0.35)';
    const camera = 'rgba(255,183,77,0.35)';
    const grid = 'rgba(255,255,255,0.14)';

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
    }

    render() {
        const container = document.createElement('div');
        container.className = 'config-row';
        container.innerHTML = `
            <label>${this.label}</label>
            <input type="range" min="${this.min}" max="${this.max}" step="${this.step}" value="${this.initialValue}">
            <span class="value-display">${this.initialValue}</span>
        `;
        const input = container.querySelector('input');
        const display = container.querySelector('.value-display');
        input.addEventListener('input', () => {
            display.textContent = input.value;
            this.currentValue = parseFloat(input.value);
        });
        this.element = container;
        if (this.disabled) this.setDisabled(true);
        return container;
    }

    getValue() { return this.currentValue; }
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

        this.setupCanvas();
        this.setupInteraction();

        return container;
    }

    getValue() {
        return this.strengthValue;
    }

    loadValue(strength, impact) {
        this.strengthValue = strength;
        this.impactValue = impact;
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
        const strengthPx = WorldScaleService.worldToPixels(this.strengthValue);
        const impactPx = WorldScaleService.worldToPixels(this.impactValue);
        return Math.max(strengthPx, impactPx, 1);
    }

    getDisplayScale() {
        const halfExtent = Math.min(this.centerX, this.centerY);
        return WorldScaleService.getCanvasFitScale(halfExtent, this.getMaxNaturalExtentPx());
    }

    setupInteraction() {
        this.canvas.addEventListener('mousedown', (e) => {
            const pos = getCanvasEventPos(this.canvas, e);
            const { up, right } = this.getTips();
            if (isNearPoint(pos, up)) {
                this.isDraggingUp = true;
            } else if (isNearPoint(pos, right)) {
                this.isDraggingRight = true;
            }
        });

        window.addEventListener('mousemove', (e) => {
            if (!this.isDraggingUp && !this.isDraggingRight) return;

            const pos = getCanvasEventPos(this.canvas, e);
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
            this.draw();
        });

        window.addEventListener('mouseup', () => {
            this.isDraggingUp = false;
            this.isDraggingRight = false;
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

        const { up, right } = this.getTips();
        const spiralRadius = this.centerY - up.y;

        ctx.strokeStyle = ControlPalette.spiral;
        ctx.lineWidth = 2;
        ctx.beginPath();
        const turns = 2.5;
        const points = 60;
        for (let i = 0; i <= points; i++) {
            const t = (i / points) * turns * Math.PI * 2;
            const r = (i / points) * spiralRadius;
            const px = this.centerX + r * Math.cos(t);
            const py = this.centerY + r * Math.sin(t);
            if (i === 0) ctx.moveTo(px, py);
            else ctx.lineTo(px, py);
        }
        ctx.stroke();

        drawControlArrow(this.ctx, { x: this.centerX, y: this.centerY }, up, ControlPalette.radius, 'up');
        drawControlArrow(this.ctx, up, right, ControlPalette.impact, 'left');
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
    }

    addRadiusListener(listener) {
        this.radiusListeners.push(listener);
    }

    notifyRadiusChange() {
        for (const listener of this.radiusListeners) {
            listener(this.radiusValue);
        }
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
        this.canvas.addEventListener('mousedown', (e) => {
            const pos = getCanvasEventPos(this.canvas, e);
            const { white, cyan, green } = this.getTips();
            if (isNearPoint(pos, white)) {
                this.isDraggingUp = true;
            } else if (isNearPoint(pos, cyan)) {
                this.isDraggingRight = true;
            } else if (isNearPoint(pos, green)) {
                this.isDraggingDiagonal = true;
            }
        });

        window.addEventListener('mousemove', (e) => {
            if (!this.isDraggingUp && !this.isDraggingRight && !this.isDraggingDiagonal) return;

            const pos = getCanvasEventPos(this.canvas, e);

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

        window.addEventListener('mouseup', () => {
            this.isDraggingUp = false;
            this.isDraggingRight = false;
            this.isDraggingDiagonal = false;
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

        const tips = this.getTips();
        const pushRadius = this.radiusValue * this.pushValue;

        // Momentum ring only (annulus), never a filled disc over the interior
        if (pushRadius > this.radiusValue) {
            this.drawAnnulus(this.radiusValue, pushRadius, ControlPalette.effectRing);
        }

        // Opaque body circle so the push ring cannot tint the interior
        this.drawFilledCircle(this.radiusValue, ControlPalette.circle);

        drawControlArrow(
            this.ctx,
            { x: this.centerX, y: this.centerY },
            tips.white,
            ControlPalette.radius,
            'up'
        );
        drawControlArrow(
            this.ctx,
            { x: this.centerX + tips.radiusPx, y: this.centerY },
            tips.cyan,
            ControlPalette.effect,
            'left'
        );
        drawControlArrow(
            this.ctx,
            { x: this.centerX, y: this.centerY },
            tips.green,
            ControlPalette.impact,
            'diagonal'
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

class SimModeControl {
    static MODES = [
        { value: 'device', label: 'GPU' },
        { value: 'hybrid', label: 'CPU', disabled: true },
    ];

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
            if (mode.disabled) {
                button.disabled = true;
                button.classList.add('input-mode-btn-disabled');
            } else {
                button.classList.add('selected');
            }
            options.appendChild(button);
        }

        return container;
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
        }
    }

    selectMode(mode) {
        if (mode === 'hand' && !this.cameraDetected) return;
        if (mode === this.currentValue) return;
        this.currentValue = mode;
        this.updateSelectionState();
        this.applyHandControlState();
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
    }

    getValue() {
        return this.normalizeValue(this.currentValue);
    }
}

class HandControl extends ConfigControl {
    static CONTENT_ASPECT = 3 / 2;
    static DOT_RADIUS_ACTIVE_MIN = 3;
    static DOT_RADIUS_ACTIVE_MAX = 5;
    static DOT_RADIUS_INACTIVE_RATIO = 2.5 / 4;

    constructor(leftConfigPath, rightConfigPath, initialLeft, initialRight) {
        super(leftConfigPath, 'Hands', initialLeft);
        this.rightConfigPath = rightConfigPath;
        this.leftMode = initialLeft || 'full';
        this.rightMode = initialRight || 'full';
        this.circleControl = null;
        this.canvas = null;
        this.ctx = null;
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
        `;

        this.canvas = container.querySelector('.hand-canvas');
        this.ctx = this.canvas.getContext('2d');
        this.element = container;

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
            this.canvas.style.cursor = disabled ? 'not-allowed' : 'pointer';
            this.canvas.style.pointerEvents = disabled ? 'none' : '';
        }
        if (disabled) {
            this.leftMode = 'none';
            this.rightMode = 'none';
            this.updateDisplay();
            this.draw();
        }
    }

    getValue() {
        return this.leftMode;
    }

    loadValue(left, right) {
        if (this.disabled) {
            left = 'none';
            right = 'none';
        }
        this.leftMode = left || 'full';
        this.rightMode = right || 'full';
        if (this.element) {
            this.updateDisplay();
            this.draw();
        }
    }

    setupCanvas() {
        setupResizableCanvas(this.canvas, () => this.draw());
    }

    setupInteraction() {
        this.canvas.style.cursor = 'pointer';

        this.canvas.addEventListener('click', (e) => {
            if (this.disabled) return;
            const { x, y } = getCanvasEventPos(this.canvas, e);
            const content = this.getContentRect();
            if (x < content.x || x > content.x + content.width ||
                y < content.y || y > content.y + content.height) {
                return;
            }

            if (x < content.x + content.width / 2) {
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
            this.leftMode, HAND_COLORS.left, true
        );
        this.drawHand(
            content.x + content.width / 2, content.y, content.width / 2, content.height,
            this.rightMode, HAND_COLORS.right, false
        );
    }

    drawHand(ox, oy, areaW, areaH, mode, activeColor, mirror) {
        const ctx = this.ctx;
        const activeSet = new Set(ACTIVE_LANDMARKS_BY_MODE[mode]);
        const grey = HAND_COLORS.grey;
        const greyDot = HAND_COLORS.greyDot;

        const pad = 0.1;
        const usableW = areaW * (1 - 2 * pad);
        const usableH = areaH * (1 - 2 * pad);
        const scaleX = usableW;
        const scaleY = usableH;
        const offsetX = ox + areaW * pad;
        const offsetY = oy + areaH * pad;

        const tx = (lm) => {
            let nx = lm.x;
            if (mirror) nx = 1 - nx;
            return offsetX + nx * scaleX;
        };
        const ty = (lm) => offsetY + lm.y * scaleY;

        const lms = HAND_LANDMARKS;

        ctx.lineWidth = 2;
        for (const [i, j] of HAND_CONNECTIONS) {
            const bothActive = mode !== 'joints' && activeSet.has(i) && activeSet.has(j);
            ctx.strokeStyle = bothActive ? activeColor : grey;
            ctx.beginPath();
            ctx.moveTo(tx(lms[i]), ty(lms[i]));
            ctx.lineTo(tx(lms[j]), ty(lms[j]));
            ctx.stroke();
        }

        for (let i = 0; i < lms.length; i++) {
            const active = activeSet.has(i);
            ctx.fillStyle = active ? activeColor : greyDot;
            const r = this.getDotRadius(active);
            ctx.beginPath();
            ctx.arc(tx(lms[i]), ty(lms[i]), r, 0, Math.PI * 2);
            ctx.fill();
        }
    }
}

class EnvironmentControl extends ConfigControl {
    static EDGE_BITS = { left: 8, top: 4, bottom: 2, right: 1 };
    static SIDE_TO_EDGE = { 0: 8, 1: 4, 2: 2, 3: 1 };
    static EDGES = [
        { key: 'left', bit: 8, side: 0 },
        { key: 'top', bit: 4, side: 1 },
        { key: 'bottom', bit: 2, side: 2 },
        { key: 'right', bit: 1, side: 3 },
    ];

    constructor(initialEdges, initialWindTunnel = {}) {
        super('simulation.edges', 'Environment', initialEdges);
        this.edgesMask = initialEdges ?? 15;
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
    }

    getAspectRatio() {
        const app = window.kataraApp;
        let domainAspect = null;
        if (app?.module?._getSimDomainWidth && app?.module?._getSimDomainHeight) {
            const domainWidth = app.module._getSimDomainWidth();
            const domainHeight = app.module._getSimDomainHeight();
            if (domainWidth > 0 && domainHeight > 0) {
                domainAspect = domainWidth / domainHeight;
            }
        }

        const inkAspect = app?.inkAspectRatio;
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

    updateCanvasContainerAspect() {
        const container = this.canvas?.parentElement;
        if (!container) return;
        container.style.aspectRatio = `${this.getAspectRatio()}`;
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
            format: (value) => value.toFixed(2),
        });

        this.widthSlider = container.querySelector('.environment-width-row');
        bindSliderRow(this.widthSlider, {
            onChange: (value) => {
                this.widthValue = value;
                this.draw();
            },
            format: (value) => value.toFixed(2),
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
        if (side !== undefined) this.windSide = side;
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
        this.canvas.style.cursor = 'pointer';

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
                this.edgesMask |= EnvironmentControl.SIDE_TO_EDGE[arrowHit];
            }
            this.updateSliderState();
            this.draw();
        }
    }

    computeLayout() {
        const w = this.canvas.width;
        const h = this.canvas.height;
        const aspect = this.getAspectRatio();
        const canvasPad = Math.max(12, Math.min(w, h) * 0.10);
        const edgeThickness = Math.max(5, Math.min(w, h) * 0.05);
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
                side: 1,
                from: { x: inner.x + inner.width / 2, y: inner.y },
                to: { x: inner.x + inner.width / 2, y: inner.y + lenY },
            },
            {
                side: 2,
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

    hitTestArrow(pos) {
        const tipThreshold = 26;
        const lineThreshold = 14;
        for (const arrow of this.getArrowSpecs()) {
            const dx = pos.x - arrow.to.x;
            const dy = pos.y - arrow.to.y;
            if (dx * dx + dy * dy < tipThreshold * tipThreshold) {
                return arrow.side;
            }
            if (this.pointToSegmentDistance(pos, arrow.from, arrow.to) <= lineThreshold) {
                return arrow.side;
            }
        }
        return null;
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

        const arrowColorOff = 'rgba(190, 190, 190, 0.34)';
        for (const arrow of this.getArrowSpecs()) {
            const active = this.windSide === arrow.side;
            const axisSize = (arrow.side === 0 || arrow.side === 3)
                ? this.layout.inner.height
                : this.layout.inner.width;
            const widthPx = axisSize * this.widthValue;
            const arrowBaseWidth = Math.max(this.layout.edgeThickness * 0.36, widthPx);
            const arrowHeadWidth = Math.max(arrowBaseWidth * 1.35, this.layout.edgeThickness * 0.95);
            const arrowHeadLength = Math.max(16, this.layout.edgeThickness * 1.12);
            this.drawArrow(
                arrow.from,
                arrow.to,
                active ? ControlPalette.impact : arrowColorOff,
                arrowBaseWidth,
                arrowHeadWidth,
                arrowHeadLength
            );
        }
    }

    fillPolygon(points, color) {
        const ctx = this.ctx;
        ctx.fillStyle = color;
        ctx.strokeStyle = color;
        ctx.lineWidth = 1.5;
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

    drawArrow(from, tip, color, baseWidth = 8, headWidth = 14, headLength = 12) {
        const ctx = this.ctx;
        ctx.fillStyle = color;
        const dx = tip.x - from.x;
        const dy = tip.y - from.y;
        const length = Math.sqrt(dx * dx + dy * dy);
        if (length < 0.001) return;
        const ux = dx / length;
        const uy = dy / length;
        const px = -uy;
        const py = ux;
        const shaftLength = Math.max(0, length - headLength);
        const neckX = from.x + ux * shaftLength;
        const neckY = from.y + uy * shaftLength;
        const baseHalf = baseWidth * 0.5;
        const headHalf = headWidth * 0.5;

        ctx.beginPath();
        ctx.moveTo(from.x + px * baseHalf, from.y + py * baseHalf);
        ctx.lineTo(neckX + px * baseHalf, neckY + py * baseHalf);
        ctx.lineTo(neckX + px * headHalf, neckY + py * headHalf);
        ctx.lineTo(tip.x, tip.y);
        ctx.lineTo(neckX - px * headHalf, neckY - py * headHalf);
        ctx.lineTo(neckX - px * baseHalf, neckY - py * baseHalf);
        ctx.lineTo(from.x - px * baseHalf, from.y - py * baseHalf);
        ctx.closePath();
        ctx.fill();
    }
}

function collectControlValue(control, values) {
    if (control instanceof SimModeControl) return;
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
    } else if (control instanceof EnvironmentControl) {
        setNestedValue(values, control.configPath, control.edgesMask);
        setNestedValue(values, 'simulation.windTunnel.side', control.windSide);
        setNestedValue(values, 'simulation.windTunnel.velocity', control.velocityValue);
        const halfWidth = control.widthValue / 2;
        setNestedValue(values, 'simulation.windTunnel.startPosition', 0.5 - halfWidth);
        setNestedValue(values, 'simulation.windTunnel.endPosition', 0.5 + halfWidth);
    } else {
        setNestedValue(values, control.configPath, control.getValue());
    }
}

function loadControlValue(control, config) {
    if (control instanceof SimModeControl) return;
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
    } else if (control instanceof InputModeControl) {
        control.loadValue(config.inputMode ?? 'hand');
    } else if (control instanceof HandControl) {
        if (control.disabled) {
            control.loadValue('none', 'none');
        } else {
            control.loadValue(
                getNestedValue(config, control.configPath),
                getNestedValue(config, control.rightConfigPath)
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
        if (control instanceof SliderControl) {
            const input = control.element.querySelector('input[type="range"]');
            const display = control.element.querySelector('.value-display');
            if (input) input.value = value;
            if (display) display.textContent = value;
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

class SettingsPanel {
    constructor(parentApp) {
        this.parentApp = parentApp;
        this.panelElement = null;
        this.sections = [];
        this.isOpen = false;
        this.wasPausedBeforeOpen = false;
        this.originalConfig = null;
        this.boundKeyHandler = null;
    }

    addSection(section) {
        this.sections.push(section);
    }

    async open() {
        if (this.isOpen) return;
        this.wasPausedBeforeOpen = this.parentApp.simulationPaused;
        if (!this.wasPausedBeforeOpen) {
            this.parentApp.togglePause();
        }
        this.saveOriginalConfig();
        this.createPanel();
        this.loadCurrentValues();
        await this.refreshEnvironmentControls();
        this.boundKeyHandler = this.handleKeyDown.bind(this);
        window.addEventListener('keydown', this.boundKeyHandler);
        this.isOpen = true;
    }

    async refreshEnvironmentControls() {
        if (this.parentApp?.updateInkAspectRatioFromConfig) {
            await this.parentApp.updateInkAspectRatioFromConfig();
        }
        const refresh = () => {
            this.sections.forEach((section) => {
                section.getAllControls().forEach((control) => {
                    if (control instanceof EnvironmentControl) {
                        control.refreshLayout();
                    }
                });
            });
        };
        refresh();
        requestAnimationFrame(refresh);
    }

    close(saveChanges = false) {
        if (!this.isOpen) return;

        if (this.boundKeyHandler) {
            window.removeEventListener('keydown', this.boundKeyHandler);
            this.boundKeyHandler = null;
        }

        if (saveChanges) {
            this.saveAndApply();
        }

        this.panelElement?.remove();

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
                <h2>Settings</h2>
                <div class="settings-panel-buttons">
                    <button class="settings-btn settings-btn-cancel" title="Discard (ESC)">
                        <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                            <line x1="18" y1="6" x2="6" y2="18"></line>
                            <line x1="6" y1="6" x2="18" y2="18"></line>
                        </svg>
                        <span>[ESC]</span>
                    </button>
                    <button class="settings-btn settings-btn-save" title="Save (Enter)">
                        <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                            <polyline points="20,6 9,17 4,12"></polyline>
                        </svg>
                        <span>[Enter]</span>
                    </button>
                </div>
            </div>
            <div class="settings-panel-content"></div>
        `;
        panel.querySelector('.settings-btn-cancel').addEventListener('click', () => this.close(false));
        panel.querySelector('.settings-btn-save').addEventListener('click', () => this.close(true));
        panel.tabIndex = -1;
        setTimeout(() => panel.focus(), 0);

        const content = panel.querySelector('.settings-panel-content');
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

        this.panelElement = panel;
        document.body.appendChild(panel);
    }

    handleKeyDown(e) {
        if (!this.isOpen) return;

        if (e.target instanceof HTMLInputElement) {
            const textTypes = ['text', 'search', 'url', 'tel', 'email', 'password', 'number'];
            if (textTypes.includes(e.target.type)) return;
        } else if (e.target instanceof HTMLTextAreaElement ||
                   e.target instanceof HTMLSelectElement ||
                   e.target.isContentEditable) {
            return;
        }

        if (e.key === 'Escape') {
            e.preventDefault();
            e.stopPropagation();
            this.close(false);
        } else if (e.key === 'Enter') {
            e.preventDefault();
            e.stopPropagation();
            this.close(true);
        }
    }

    loadCurrentValues() {
        if (!this.originalConfig) return;
        const config = this.originalConfig;
        this.sections.forEach(section => {
            section.getAllControls().forEach(control => loadControlValue(control, config));
        });
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
        const mergedConfig = deepMerge(deepMerge({}, this.originalConfig || {}), this.collectAllValues());
        const sectionChanged = (key) =>
            JSON.stringify(this.originalConfig?.[key] ?? {}) !== JSON.stringify(mergedConfig?.[key] ?? {});
        const layoutChanged = sectionChanged('layout');
        const simulationChanged = sectionChanged('simulation');
        const renderingChanged = sectionChanged('rendering');
        try {
            this.parentApp.mergeViewportTargetsIntoConfig(mergedConfig);
            const configText = JSON.stringify(mergedConfig, null, 4);
            this.parentApp.module.FS.writeFile('/config.json', configText);

            if (this.parentApp.idbfsAvailable) {
                this.parentApp.module.FS.writeFile('/persist/config.json', configText);
                await new Promise((resolve) => {
                    this.parentApp.module.FS.syncfs(false, (err) => {
                        if (err) console.error('syncfs failed:', err);
                        resolve();
                    });
                });
                localStorage.setItem('katara_needs_restore', 'true');
            }

            window.kataraConfig = mergedConfig;

            if (this.parentApp.setInputMode) {
                this.parentApp.setInputMode(mergedConfig.inputMode ?? 'hand', { persist: false });
            }

            if (this.parentApp.module?._reloadConfig) {
                try {
                    let flags = 0;
                    if (simulationChanged) flags |= 2;
                    if (renderingChanged) flags |= 4;
                    if (layoutChanged) flags |= (4 | 8);
                    if (flags !== 0) {
                        this.parentApp.module._reloadConfig(flags);
                    }
                } catch (e) {
                    console.error('reloadConfig error:', e);
                }
            }

            if (simulationChanged && this.parentApp.module?._resetFluidField) {
                this.parentApp.module._resetFluidField();
            }

            this.parentApp.syncLayoutStateFromConfig();
            this.parentApp.updateCameraUi();
            if (layoutChanged || renderingChanged) {
                this.parentApp.refreshSimLayout();
            } else {
                this.parentApp.updatePlotLabels();
                this.parentApp.updateViewportButtons();
            }
        } catch (err) {
            console.error('Failed to save config:', err);
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

    const simSection = new ConfigSection('Simulation', { layout: 'grid2' });
    simSection.addHeaderControl(new SimModeControl());
    simSection.addControl(new SliderControl('simulation.resolution', 'Resolution', sim.resolution ?? 400, 50, 800, 50));
    simSection.addControl(new SliderControl('simulation.timestep', 'Timestep', sim.timestep ?? 0.02, 0.001, 0.1, 0.001));
    simSection.addControl(new SliderControl('simulation.gravity', 'Gravity', sim.gravity ?? 0, -10, 10, 0.1));
    simSection.addControl(new SliderControl('simulation.fluidDensity', 'Density', sim.fluidDensity ?? 1000, 100, 5000, 100));
    sections.push(simSection);

    const projSection = new ConfigSection('Pressure Solver', { layout: 'grid2' });
    const overrelaxationControl = new SliderControl(
        'simulation.projection.overrelaxationCoefficient',
        'Overrelaxation',
        proj.overrelaxationCoefficient ?? 1.0,
        0,
        2,
        0.1
    );
    if (isGpuSimulatorMode(currentConfig)) {
        overrelaxationControl.setDisabled(true);
    }
    projSection.addControl(overrelaxationControl);
    projSection.addControl(new SliderControl('simulation.projection.iterations', 'Iterations', proj.iterations ?? 200, 100, 1000, 50));
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
    plotsSection.addControl(new CheckboxControl('layout.components.density_histogram.enabled', 'Density', currentConfig.layout?.components?.density_histogram?.enabled ?? true));
    plotsSection.addControl(new CheckboxControl('layout.components.velocity_histogram.enabled', 'Velocity', currentConfig.layout?.components?.velocity_histogram?.enabled ?? true));
    plotsSection.addControl(new CheckboxControl('layout.components.entropy_time_series.enabled', 'Entropy', currentConfig.layout?.components?.entropy_time_series?.enabled ?? true));
    plotsSection.addControl(new CheckboxControl('layout.components.volume_time_series.enabled', 'Volume', currentConfig.layout?.components?.volume_time_series?.enabled ?? false));
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
        cameraDetected ? (hands.left ?? 'full') : 'none',
        cameraDetected ? (hands.right ?? 'full') : 'none'
    );
    handControl.linkCircleControl(circleControl);
    inputModeControl.linkHandControl(handControl, cameraDetected);
    if (!cameraDetected) {
        handControl.collectWhenDisabled = true;
        handControl.setDisabled(true);
    }
    interactionSection.addControl(circleControl);
    interactionSection.addControl(handControl);
    sections.push(interactionSection);

    const windTunnel = sim.windTunnel ?? {};
    const envSection = new ConfigSection('Environment', { column: 'right', placement: 'bottom' });
    const envControl = new EnvironmentControl(sim.edges ?? 15, windTunnel);
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
    measureElementWidth,
    measureElementHeight,
    getSimViewportSize,
    HAND_CONNECTIONS,
    ACTIVE_LANDMARKS_BY_MODE,
    HAND_COLORS,
};
