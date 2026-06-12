/**
 * Katara UI palette — single source of truth for web UI colors.
 * installKataraPalette() injects CSS custom properties used by styles.css.
 * fragment.wgsl void background uses Palette.black (--offblack on charliemax.dev) — keep in sync manually.
 */

export const Palette = {
    black: '#050505',
    surface: '#0a0a0a',
    surfaceRaised: '#1a1a1a',

    white: 'rgba(255, 255, 255, 0.9)',
    whiteFull: '#ffffff',
    whiteBright: 'rgba(255, 255, 255, 0.95)',
    whiteMuted: 'rgba(255, 255, 255, 0.8)',
    whiteSoft: 'rgba(255, 255, 255, 0.6)',
    whiteFaint: 'rgba(255, 255, 255, 0.55)',
    whiteGhost: 'rgba(255, 255, 255, 0.45)',

    grey: 'rgba(170, 170, 170, 0.95)',
    greyDim: 'rgba(170, 170, 170, 0.35)',
    greyInactive: 'rgba(190, 190, 190, 0.34)',
    wireGrey: 'rgba(255, 255, 255, 0.18)',

    green: '#00ff88',
    blue: '#64b5f6',
    orange: '#ff9933',

    buttonGreen: '#4CAF50',
    buttonGreenHover: '#66bb6a',

    red: '#f44336',
    redHover: '#ef5350',

    warn: '#ffd54f',

    borderWidth: '2px',
    border: 'rgba(255, 255, 255, 0.2)',
    borderHover: 'rgba(255, 255, 255, 0.45)',
    borderInput: 'rgba(255, 255, 255, 0.35)',
    borderFocus: 'rgba(255, 255, 255, 0.4)',

    surfaceFill: 'rgba(255, 255, 255, 0.05)',
    surfaceFillHover: 'rgba(255, 255, 255, 0.08)',
    surfaceFillSubtle: 'rgba(255, 255, 255, 0.04)',
    surfaceFillStrong: 'rgba(255, 255, 255, 0.1)',
    surfaceFillInput: 'rgba(255, 255, 255, 0.08)',

    canvasWell: 'rgba(0, 0, 0, 0.3)',
    overlayDark: 'rgba(0, 0, 0, 0.55)',
    overlayDarkHover: 'rgba(0, 0, 0, 0.7)',
    layoutPreviewBg: 'rgba(0, 0, 0, 0.24)',
    panelShadow: 'rgba(0, 0, 0, 0.5)',

    gridLine: 'rgba(255, 255, 255, 0.14)',
    spiral: 'rgba(255, 255, 255, 0.85)',

    scrollbarThumb: 'rgba(255, 255, 255, 0.2)',
    scrollbarThumbHover: 'rgba(255, 255, 255, 0.3)',
};

export function colorWithAlpha(color, alpha) {
    if (color.startsWith('#')) {
        const hex = color.slice(1);
        const full = hex.length === 3
            ? hex.split('').map((c) => c + c).join('')
            : hex;
        const r = parseInt(full.slice(0, 2), 16);
        const g = parseInt(full.slice(2, 4), 16);
        const b = parseInt(full.slice(4, 6), 16);
        return `rgba(${r}, ${g}, ${b}, ${alpha})`;
    }
    const match = color.match(/rgba?\(([^)]+)\)/);
    if (!match) return color;
    const parts = match[1].split(',').map((part) => part.trim());
    const [r, g, b] = parts;
    return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

export function layoutTint(color, alpha = 0.35) {
    return colorWithAlpha(color, alpha);
}

export const ControlPalette = {
    spiral: Palette.spiral,
    circle: Palette.grey,
    circleMouse: Palette.greyDim,
    radius: colorWithAlpha(Palette.green, 0.95),
    impact: colorWithAlpha(Palette.blue, 0.95),
    effect: colorWithAlpha(Palette.orange, 0.75),
    effectRing: colorWithAlpha(Palette.orange, 0.22),
};

export const HandPalette = {
    left: Palette.green,
    right: Palette.orange,
    grey: Palette.wireGrey,
    greyDot: Palette.borderInput,
    overlayGrey: Palette.border,
};

function toCssVarName(key) {
    return `--katara-${key.replace(/[A-Z]/g, (match) => `-${match.toLowerCase()}`)}`;
}

let paletteInstalled = false;

export function installKataraPalette(root = document.documentElement) {
    if (paletteInstalled || typeof document === 'undefined') return;
    paletteInstalled = true;

    const derived = {
        borderStyle: `${Palette.borderWidth} solid ${Palette.border}`,
        buttonGreenBorder: colorWithAlpha(Palette.buttonGreen, 0.6),
        buttonGreenBg: colorWithAlpha(Palette.buttonGreen, 0.16),
        buttonGreenBgHover: colorWithAlpha(Palette.buttonGreen, 0.24),
        redBorder: colorWithAlpha(Palette.red, 0.6),
        redBg: colorWithAlpha(Palette.red, 0.16),
        redBgHover: colorWithAlpha(Palette.red, 0.24),
        blueSelectedBorder: colorWithAlpha(Palette.blue, 0.95),
        blueSelectedBg: colorWithAlpha(Palette.blue, 0.16),
        warnBorder: colorWithAlpha(Palette.warn, 0.85),
        warnBorderStrong: colorWithAlpha(Palette.warn, 0.95),
        warnBg: colorWithAlpha(Palette.warn, 0.1),
        warnBgHover: colorWithAlpha(Palette.warn, 0.14),
        warnInset: colorWithAlpha(Palette.warn, 0.2),
        layoutStroke: Palette.whiteFaint,
        layoutViewport: layoutTint(Palette.blue),
        layoutPlot: layoutTint(Palette.green),
        layoutCamera: layoutTint(Palette.orange),
        layoutGrid: Palette.gridLine,
    };

    for (const [key, value] of Object.entries({ ...Palette, ...derived })) {
        root.style.setProperty(toCssVarName(key), value);
    }
}

installKataraPalette();
