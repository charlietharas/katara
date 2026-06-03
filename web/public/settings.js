// SCALING UTILITY
class WorldScaleService {
    static getPixelsPerWorldUnit() {
        const viewport = document.querySelector('.sim-viewport');
        if (!viewport) return 10; // fallback

        const viewportWidth = viewport.clientWidth;
        const viewportHeight = viewport.clientHeight;

        // get grid resolution from config if available
        const gridResolution = WorldScaleService.getGridResolution() || 400;

        // use the larger dimension for scaling
        const maxDimension = Math.max(viewportWidth, viewportHeight);
        return maxDimension / gridResolution;
    }

    // TODO jank review this
    static getGridResolution() {
        // try to get from current config in the DOM if settings panel is open
        const configDisplay = document.querySelector('.vorticity-strength-value');
        if (configDisplay) {
            // settings panel is open, try to get from window.kataraConfig if available
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
}

// LAYOUTS
const LAYOUT_PRESET_OPTIONS = [
    { value: 'default', label: 'Dashboard' },
    { value: 'focused', label: 'Focused' },
    { value: 'viewport', label: 'Viewport' },
    { value: 'gallery_single', label: 'Gallery (single)' },
    { value: 'gallery_double', label: 'Gallery (double)' },
    { value: 'gallery_quad', label: 'Gallery (quad)' }
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

// CONFIG
class ConfigControl {
    constructor(configPath, label, initialValue) {
        this.configPath = configPath;
        this.label = label;
        this.initialValue = initialValue;
        this.currentValue = initialValue;
        this.element = null;
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
        if (checkbox) {
            // keep checkbox keyboard handling from intercepting enter
            checkbox.addEventListener('keydown', (e) => {
                if (e.key === 'Enter' || e.key === ' ') {
                    e.preventDefault();
                }
            });
        }
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
        return container;
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

// CUSTOM
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
        const resizeCanvas = () => {
            const rect = this.canvas.parentElement.getBoundingClientRect();
            this.canvas.width = rect.width;
            this.canvas.height = rect.height;
            this.centerX = this.canvas.width / 2;
            this.centerY = this.canvas.height / 2;
            this.draw();
        };

        resizeCanvas();
        new ResizeObserver(resizeCanvas).observe(this.canvas.parentElement);
    }

    setupInteraction() {
        const getMousePos = (e) => {
            const rect = this.canvas.getBoundingClientRect();
            return { x: e.clientX - rect.left, y: e.clientY - rect.top };
        };

        const nearTip = (pos, tip, threshold = 15) => {
            const dx = pos.x - tip.x;
            const dy = pos.y - tip.y;
            return dx * dx + dy * dy < threshold * threshold;
        };

        this.canvas.addEventListener('mousedown', (e) => {
            const pos = getMousePos(e);
            const { up, right } = this.getTips();
            if (nearTip(pos, up)) {
                this.isDraggingUp = true;
            } else if (nearTip(pos, right)) {
                this.isDraggingRight = true;
            }
        });

        window.addEventListener('mousemove', (e) => {
            if (!this.isDraggingUp && !this.isDraggingRight) return;

            const pos = getMousePos(e);
            if (this.isDraggingUp) {
                const dy = Math.max(0, this.centerY - pos.y);
                this.strengthValue = Math.max(0, Math.min(this.maxStrength, WorldScaleService.pixelsToWorld(dy)));
            } else {
                const { up } = this.getTips();
                const dx = Math.max(0, pos.x - up.x);
                this.impactValue = Math.max(1, Math.min(this.maxImpact, WorldScaleService.pixelsToWorld(dx)));
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
        const up = {
            x: this.centerX,
            y: this.centerY - WorldScaleService.worldToPixels(this.strengthValue)
        };
        return {
            up,
            right: {
                x: up.x + WorldScaleService.worldToPixels(this.impactValue),
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

        ctx.strokeStyle = 'rgba(255, 255, 255, 0.8)';
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

        this.drawArrow({ x: this.centerX, y: this.centerY }, up, 'rgba(76, 175, 80, 0.9)', 'up');
        this.drawArrow(up, right, 'rgba(100, 181, 246, 0.9)', 'left');
    }

    drawArrow(from, tip, color, headDir) {
        const ctx = this.ctx;
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
        } else {
            ctx.moveTo(tip.x, tip.y);
            ctx.lineTo(tip.x - headSize, tip.y - headSize / 2);
            ctx.lineTo(tip.x - headSize, tip.y + headSize / 2);
        }
        ctx.closePath();
        ctx.fill();

        ctx.beginPath();
        ctx.arc(tip.x, tip.y, 5, 0, Math.PI * 2);
        ctx.fill();
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
        this.radiusValue = radius;
        this.pushValue = push;
        this.strengthValue = strength;
        if (this.element) {
            this.updateDisplay();
            this.draw();
        }
    }

    setupCanvas() {
        const resizeCanvas = () => {
            const rect = this.canvas.parentElement.getBoundingClientRect();
            this.canvas.width = rect.width;
            this.canvas.height = rect.height;
            this.centerX = this.canvas.width / 2;
            this.centerY = this.canvas.height / 2;
            this.draw();
        };

        resizeCanvas();
        new ResizeObserver(resizeCanvas).observe(this.canvas.parentElement);
    }

    setupInteraction() {
        const getMousePos = (e) => {
            const rect = this.canvas.getBoundingClientRect();
            return { x: e.clientX - rect.left, y: e.clientY - rect.top };
        };

        const nearTip = (pos, tip, threshold = 15) => {
            const dx = pos.x - tip.x;
            const dy = pos.y - tip.y;
            return dx * dx + dy * dy < threshold * threshold;
        };

        this.canvas.addEventListener('mousedown', (e) => {
            const pos = getMousePos(e);
            const { white, cyan, green } = this.getTips();
            if (nearTip(pos, white)) {
                this.isDraggingUp = true;
            } else if (nearTip(pos, cyan)) {
                this.isDraggingRight = true;
            } else if (nearTip(pos, green)) {
                this.isDraggingDiagonal = true;
            }
        });

        window.addEventListener('mousemove', (e) => {
            if (!this.isDraggingUp && !this.isDraggingRight && !this.isDraggingDiagonal) return;

            const pos = getMousePos(e);

            if (this.isDraggingUp) {
                const anchorRadius = this.radiusToPixels(this.greyAnchor);
                const maxRadius = this.radiusToPixels(this.blueAnchor);
                const dy = Math.max(0, this.centerY - anchorRadius - pos.y);
                const maxDy = maxRadius - anchorRadius;
                const t = Math.max(0, Math.min(1, dy / maxDy));
                this.radiusValue = this.greyAnchor + t * (this.blueAnchor - this.greyAnchor);
            } else if (this.isDraggingRight) {
                const { radiusPx } = this.getTips();
                const maxExtension = this.radiusToPixels(this.blueAnchor);
                const dx = Math.max(0, pos.x - (this.centerX + radiusPx));
                const maxDx = Math.max(10, maxExtension - radiusPx);
                const t = Math.max(0, Math.min(1, dx / maxDx));
                this.pushValue = 1.0 + t;
            } else {
                const dx = Math.max(0, pos.x - this.centerX);
                const dy = Math.max(0, this.centerY - pos.y);
                const alongWorld = WorldScaleService.pixelsToWorld((dx + dy) / Math.SQRT2);
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
        const greyAnchorPx = this.radiusToPixels(this.greyAnchor);
        const radiusPx = this.radiusToPixels(this.radiusValue);
        const pushPx = this.radiusToPixels(this.radiusValue * this.pushValue);
        const cos45 = Math.SQRT1_2;
        const strengthPx = WorldScaleService.worldToPixels(this.strengthValue);

        return {
            greyAnchorPx,
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
            this.drawAnnulus(this.radiusValue, pushRadius, 'rgba(0, 200, 200, 0.45)');
        }

        // Opaque body circle so the cyan ring cannot tint the interior
        this.drawFilledCircle(this.radiusValue, 'rgba(170, 170, 170, 0.95)');

        this.drawArrow(
            { x: this.centerX, y: this.centerY - tips.greyAnchorPx },
            tips.white,
            'rgba(255, 255, 255, 0.9)',
            'up'
        );
        this.drawArrow(
            { x: this.centerX + tips.radiusPx, y: this.centerY },
            tips.cyan,
            'rgba(0, 200, 200, 0.9)',
            'left'
        );
        this.drawArrow(
            { x: this.centerX, y: this.centerY },
            tips.green,
            'rgba(76, 175, 80, 0.9)',
            'diagonal'
        );
    }

    drawFilledCircle(domainRadius, color) {
        const ctx = this.ctx;
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.arc(this.centerX, this.centerY, this.radiusToPixels(domainRadius), 0, Math.PI * 2);
        ctx.fill();
    }

    drawAnnulus(innerDomainRadius, outerDomainRadius, fillStyle) {
        const innerPx = this.radiusToPixels(innerDomainRadius);
        const outerPx = this.radiusToPixels(outerDomainRadius);
        if (outerPx <= innerPx + 0.5) return;

        const ctx = this.ctx;
        ctx.fillStyle = fillStyle;
        ctx.beginPath();
        ctx.arc(this.centerX, this.centerY, outerPx, 0, Math.PI * 2);
        ctx.arc(this.centerX, this.centerY, innerPx, 0, Math.PI * 2, true);
        ctx.fill('evenodd');
    }

    drawArrow(from, tip, color, headDir) {
        const ctx = this.ctx;
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
}

class HandControl extends ConfigControl {
    static MODES = ['full', 'pointer', 'pointer-tip', 'none'];
    static MODE_LABELS = { 'full': 'Full', 'pointer': 'Pointer', 'pointer-tip': 'Dot', 'none': 'Off' };
    static COLORS = {
        left: '#00ff88',
        right: '#ff9933',
        grey: 'rgba(255, 255, 255, 0.18)',
        greyDot: 'rgba(255, 255, 255, 0.3)'
    };

    static ACTIVE_LANDMARKS = {
        'full': [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20],
        'pointer': [5,6,7,8],
        'pointer-tip': [8],
        'none': []
    };

    static HAND_LANDMARKS = [
        {x: 0.50, y: 0.90}, // 0: wrist
        {x: 0.28, y: 0.76}, // 1: thumb CMC
        {x: 0.18, y: 0.60}, // 2: thumb MCP
        {x: 0.13, y: 0.44}, // 3: thumb IP
        {x: 0.10, y: 0.28}, // 4: thumb tip
        {x: 0.35, y: 0.42}, // 5: index MCP
        {x: 0.32, y: 0.26}, // 6: index PIP
        {x: 0.30, y: 0.12}, // 7: index DIP
        {x: 0.28, y: 0.00}, // 8: index tip
        {x: 0.48, y: 0.38}, // 9: middle MCP
        {x: 0.48, y: 0.21}, // 10: middle PIP
        {x: 0.48, y: 0.08}, // 11: middle DIP
        {x: 0.48, y: 0.00}, // 12: middle tip
        {x: 0.60, y: 0.42}, // 13: ring MCP
        {x: 0.62, y: 0.27}, // 14: ring PIP
        {x: 0.63, y: 0.14}, // 15: ring DIP
        {x: 0.64, y: 0.03}, // 16: ring tip
        {x: 0.70, y: 0.48}, // 17: pinky MCP
        {x: 0.74, y: 0.36}, // 18: pinky PIP
        {x: 0.77, y: 0.25}, // 19: pinky DIP
        {x: 0.80, y: 0.15}, // 20: pinky tip
    ];

    static HAND_CONNECTIONS = [
        [0, 1], [1, 2], [2, 3], [3, 4],
        [0, 5], [5, 6], [6, 7], [7, 8],
        [0, 9], [9, 10], [10, 11], [11, 12],
        [0, 13], [13, 14], [14, 15], [15, 16],
        [0, 17], [17, 18], [18, 19], [19, 20],
        [5, 9], [9, 13], [13, 17]
    ];

    constructor(leftConfigPath, rightConfigPath, initialLeft, initialRight) {
        super(leftConfigPath, 'Hands', initialLeft);
        this.rightConfigPath = rightConfigPath;
        this.leftMode = initialLeft || 'full';
        this.rightMode = initialRight || 'full';
        this.canvas = null;
        this.ctx = null;
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
                    <span class="hand-value-label" style="color:${HandControl.COLORS.left}">L:</span>
                    <span class="hand-left-mode">${HandControl.MODE_LABELS[this.leftMode]}</span>
                </div>
                <div class="hand-value-item">
                    <span class="hand-value-label" style="color:${HandControl.COLORS.right}">R:</span>
                    <span class="hand-right-mode">${HandControl.MODE_LABELS[this.rightMode]}</span>
                </div>
            </div>
        `;

        this.canvas = container.querySelector('.hand-canvas');
        this.ctx = this.canvas.getContext('2d');
        this.element = container;

        this.setupCanvas();
        this.setupInteraction();

        return container;
    }

    getValue() {
        return this.leftMode;
    }

    loadValue(left, right) {
        this.leftMode = left || 'full';
        this.rightMode = right || 'full';
        if (this.element) {
            this.updateDisplay();
            this.draw();
        }
    }

    setupCanvas() {
        const resizeCanvas = () => {
            const rect = this.canvas.parentElement.getBoundingClientRect();
            this.canvas.width = rect.width;
            this.canvas.height = rect.height;
            this.draw();
        };
        resizeCanvas();
        new ResizeObserver(resizeCanvas).observe(this.canvas.parentElement);
    }

    setupInteraction() {
        this.canvas.style.cursor = 'pointer';

        this.canvas.addEventListener('click', (e) => {
            const rect = this.canvas.getBoundingClientRect();
            const x = e.clientX - rect.left;
            const midX = this.canvas.width / 2;

            if (x < midX) {
                const idx = HandControl.MODES.indexOf(this.leftMode);
                this.leftMode = HandControl.MODES[(idx + 1) % HandControl.MODES.length];
            } else {
                const idx = HandControl.MODES.indexOf(this.rightMode);
                this.rightMode = HandControl.MODES[(idx + 1) % HandControl.MODES.length];
            }

            this.updateDisplay();
            this.draw();
        });
    }

    updateDisplay() {
        const leftDisplay = this.element.querySelector('.hand-left-mode');
        const rightDisplay = this.element.querySelector('.hand-right-mode');
        if (leftDisplay) leftDisplay.textContent = HandControl.MODE_LABELS[this.leftMode];
        if (rightDisplay) rightDisplay.textContent = HandControl.MODE_LABELS[this.rightMode];
    }

    draw() {
        const ctx = this.ctx;
        const w = this.canvas.width;
        const h = this.canvas.height;
        ctx.clearRect(0, 0, w, h);

        // left
        this.drawHand(0, 0, w / 2, h, this.leftMode, HandControl.COLORS.left, true);
        // right
        this.drawHand(w / 2, 0, w / 2, h, this.rightMode, HandControl.COLORS.right, false);

        // divider
        // ctx.strokeStyle = 'rgba(255, 255, 255, 0.12)';
        // ctx.lineWidth = 1;
        // ctx.beginPath();
        // ctx.moveTo(w / 2, 0);
        // ctx.lineTo(w / 2, h);
        // ctx.stroke();
    }

    drawHand(ox, oy, areaW, areaH, mode, activeColor, mirror) {
        const ctx = this.ctx;
        const activeSet = new Set(HandControl.ACTIVE_LANDMARKS[mode]);
        const grey = HandControl.COLORS.grey;
        const greyDot = HandControl.COLORS.greyDot;

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

        const lms = HandControl.HAND_LANDMARKS;

        // draw connections
        ctx.lineWidth = 2;
        for (const [i, j] of HandControl.HAND_CONNECTIONS) {
            const bothActive = activeSet.has(i) && activeSet.has(j);
            ctx.strokeStyle = bothActive ? activeColor : grey;
            ctx.beginPath();
            ctx.moveTo(tx(lms[i]), ty(lms[i]));
            ctx.lineTo(tx(lms[j]), ty(lms[j]));
            ctx.stroke();
        }

        // draw landmarks
        for (let i = 0; i < lms.length; i++) {
            const active = activeSet.has(i);
            ctx.fillStyle = active ? activeColor : greyDot;
            const r = active ? 4 : 2.5;
            ctx.beginPath();
            ctx.arc(tx(lms[i]), ty(lms[i]), r, 0, Math.PI * 2);
            ctx.fill();
        }
    }
}

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

class ConfigSection {
    constructor(title, options = {}) {
        this.title = title;
        this.controls = [];
        this.headerControls = [];
        this.layout = options.layout ?? 'column';
        this.column = options.column ?? 'left';
        this.placement = options.placement ?? 'top';
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
        } else {
            renderControls(content);
        }

        return section;
    }

    collectValues() {
        const values = {};
        this.getAllControls().forEach(control => {
            if (control instanceof VorticityControl) {
                setNestedValue(values, control.configPath, control.strengthValue);
                setNestedValue(values, control.impactConfigPath, control.impactValue);
                return;
            }
            if (control instanceof CircleControl) {
                setNestedValue(values, control.configPath, control.radiusValue);
                setNestedValue(values, control.pushConfigPath, control.pushValue);
                setNestedValue(values, control.strengthConfigPath, control.strengthValue);
                return;
            }
            if (control instanceof HandControl) {
                setNestedValue(values, control.configPath, control.leftMode);
                setNestedValue(values, control.rightConfigPath, control.rightMode);
                return;
            }
            setNestedValue(values, control.configPath, control.getValue());
        });
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

    open() {
        if (this.isOpen) return;
        this.wasPausedBeforeOpen = this.parentApp.simulationPaused;
        if (!this.wasPausedBeforeOpen) {
            this.parentApp.togglePause();
        }
        this.saveOriginalConfig();
        this.createPanel();
        this.loadCurrentValues();
        this.boundKeyHandler = this.handleKeyDown.bind(this);
        window.addEventListener('keydown', this.boundKeyHandler);
        this.isOpen = true;
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
        const windPlaceholder = document.createElement('div');
        windPlaceholder.className = 'settings-col-right-placeholder';
        windPlaceholder.textContent = 'TODO: Wind';
        rightBottom.appendChild(windPlaceholder);
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
            section.getAllControls().forEach(control => {
                if (control instanceof VorticityControl) {
                    const strength = getNestedValue(config, control.configPath);
                    const impact = getNestedValue(config, control.impactConfigPath);
                    if (strength !== undefined && impact !== undefined) {
                        control.loadValue(strength, impact);
                    }
                    return;
                }

                if (control instanceof CircleControl) {
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
                    return;
                }

                if (control instanceof HandControl) {
                    const left = getNestedValue(config, control.configPath);
                    const right = getNestedValue(config, control.rightConfigPath);
                    control.loadValue(left, right);
                    return;
                }

                if (control instanceof LayoutPresetControl) {
                    const value = getNestedValue(config, control.configPath);
                    if (value !== undefined) control.loadValue(value);
                    return;
                }

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
            });
        });
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

            if (simulationChanged && this.parentApp.module?._resetFluidField) {
                this.parentApp.module._resetFluidField();
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

function createSkeletonSections(currentConfig) {
    const sections = [];
    const sim = currentConfig.simulation ?? {};
    const proj = sim.projection ?? {};
    const vorticity = sim.vorticity ?? {};
    const layout = currentConfig.layout ?? {};

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
    layoutSection.addHeaderControl(new HeaderCheckboxControl(
        'layout.camerasEnabled',
        'Camera',
        layout.camerasEnabled ?? true
    ));
    sections.push(layoutSection);

    const plotsSection = new ConfigSection('Plots', { layout: 'grid4' });
    plotsSection.addControl(new CheckboxControl('layout.components.density_histogram.enabled', 'Density', currentConfig.layout?.components?.density_histogram?.enabled ?? true));
    plotsSection.addControl(new CheckboxControl('layout.components.velocity_histogram.enabled', 'Velocity', currentConfig.layout?.components?.velocity_histogram?.enabled ?? true));
    plotsSection.addControl(new CheckboxControl('layout.components.entropy_time_series.enabled', 'Entropy', currentConfig.layout?.components?.entropy_time_series?.enabled ?? true));
    plotsSection.addControl(new CheckboxControl('layout.components.volume_time_series.enabled', 'Volume', currentConfig.layout?.components?.volume_time_series?.enabled ?? true));
    sections.push(plotsSection);

    const vortSection = new ConfigSection('Vorticity', { column: 'right', placement: 'top' });
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

    const circleSection = new ConfigSection('Interaction', { column: 'right', placement: 'top' });
    const circle = currentConfig.simulation?.circle ?? {};
    const circleControl = new CircleControl(
        'simulation.circle.radius',
        'simulation.circle.momentumTransferRadius',
        'simulation.circle.momentumTransferStrength',
        circle.radius ?? 0.02,
        Math.max(1.0, Math.min(2.0, circle.momentumTransferRadius ?? 2.0)),
        circle.momentumTransferStrength ?? 5.0
    );
    circleSection.addControl(circleControl);
    sections.push(circleSection);

    const handsSection = new ConfigSection('Hands', { column: 'right', placement: 'bottom' });
    const hands = currentConfig.hands ?? {};
    const handControl = new HandControl(
        'hands.left',
        'hands.right',
        hands.left ?? 'full',
        hands.right ?? 'full'
    );
    handsSection.addControl(handControl);
    sections.push(handsSection);

    const simSection = new ConfigSection('Simulation', { layout: 'grid2' });
    simSection.addControl(new SliderControl('simulation.resolution', 'Resolution', sim.resolution ?? 400, 50, 800, 50));
    simSection.addControl(new SliderControl('simulation.timestep', 'Timestep', sim.timestep ?? 0.02, 0.001, 0.1, 0.001));
    simSection.addControl(new SliderControl('simulation.gravity', 'Gravity', sim.gravity ?? 0, -10, 10, 0.1));
    simSection.addControl(new SliderControl('simulation.fluidDensity', 'Density', sim.fluidDensity ?? 1000, 100, 5000, 100));
    sections.push(simSection);

    const projSection = new ConfigSection('Pressure Solver', { layout: 'grid2' });
    projSection.addControl(new SliderControl('simulation.projection.overrelaxationCoefficient', 'Overrelaxation', proj.overrelaxationCoefficient ?? 1.0, 0, 2, 0.1));
    projSection.addControl(new SliderControl('simulation.projection.iterations', 'Iterations', proj.iterations ?? 200, 100, 1000, 50));
    sections.push(projSection);

    return sections;
}

export { SettingsPanel, createSkeletonSections };
