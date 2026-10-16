export const SHADER_TAB = 'shader';

// Keep in sync with kViewDrawOpen / kViewDrawClose in render_views.cpp.
export const VIEW_DRAW_OPEN =
`fn draw(
    texCoord: vec2<i32>,
    pressureTexture: texture_2d<f32>,
    densityTexture: texture_2d<f32>,
    velocityTexture: texture_2d<f32>,
    solidTexture: texture_2d<f32>,
    inkTexture: texture_2d<f32>,
) -> vec4<f32> {
`;

export const VIEW_DRAW_CLOSE = '}';

export const VIEW_DRAW_OPEN_COLLAPSED = 'fn draw(';
export const HELPERS_EMPTY_PLACEHOLDER = '// none';

export function renderShaderDrawCollapseIcon(collapsed) {
    // Chevron points up when expanded (click to collapse), down when collapsed.
    if (collapsed) {
        return `
            <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <polyline points="6 9 12 15 18 9"></polyline>
            </svg>
        `;
    }
    return `
        <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <polyline points="18 15 12 9 6 15"></polyline>
        </svg>
    `;
}

export function splitViewSource(source) {
    const text = source ?? '';
    const marker = 'fn draw(';
    const idx = text.indexOf(marker);
    if (idx === -1) {
        return {
            helpers: text,
            header: VIEW_DRAW_OPEN,
            body: '',
            footer: VIEW_DRAW_CLOSE,
        };
    }

    const helpersRaw = text.slice(0, idx).replace(/\s+$/, '');
    const braceIdx = text.indexOf('{', idx);
    if (braceIdx === -1) {
        return {
            helpers: helpersRaw,
            header: VIEW_DRAW_OPEN,
            body: text.slice(idx),
            footer: VIEW_DRAW_CLOSE,
        };
    }

    let depth = 0;
    let end = -1;
    for (let i = braceIdx; i < text.length; i++) {
        const ch = text[i];
        if (ch === '{') depth += 1;
        else if (ch === '}') {
            depth -= 1;
            if (depth === 0) {
                end = i;
                break;
            }
        }
    }

    let body = end === -1
        ? text.slice(braceIdx + 1)
        : text.slice(braceIdx + 1, end);
    if (body.startsWith('\n')) {
        body = body.slice(1);
    }
    body = body.replace(/\s+$/, '');

    return {
        helpers: helpersRaw,
        header: VIEW_DRAW_OPEN,
        body,
        footer: VIEW_DRAW_CLOSE,
    };
}

export function joinViewSource(helpers, body) {
    const helpersTrimmed = (helpers ?? '').replace(/\s+$/, '');
    const bodyTrimmed = (body ?? '').replace(/^\n/, '').replace(/\s+$/, '');
    const prefix = helpersTrimmed ? `${helpersTrimmed}\n` : '';
    return `${prefix}${VIEW_DRAW_OPEN}${bodyTrimmed}\n${VIEW_DRAW_CLOSE}\n`;
}

export function renderShaderWorkspaceShell() {
    return `
        <div class="shader-workspace">
            <div class="shader-explorer">
                <div class="shader-explorer-list"></div>
                <div class="shader-explorer-pin"></div>
            </div>
            <div class="shader-editor-column">
                <div class="shader-editor-toolbar">
                    <span class="shader-editor-title"></span>
                    <button type="button" class="input-mode-btn shader-editor-edit">EDIT</button>
                    <button type="button" class="input-mode-btn shader-editor-save hidden">SAVE</button>
                    <button type="button" class="input-mode-btn shader-editor-cancel hidden">CANCEL</button>
                </div>
                <div class="shader-editor-panels shader-view-panels">
                    <div class="shader-editor-shell shader-helpers-shell">
                        <div class="shader-panel-label-row">
                            <div class="shader-panel-label">helpers</div>
                        </div>
                        <textarea class="shader-editor shader-helpers-editor" spellcheck="false" readonly></textarea>
                    </div>
                    <div class="shader-editor-shell shader-draw-shell">
                        <div class="shader-panel-label-row">
                            <div class="shader-panel-label">draw</div>
                            <button type="button" class="shader-draw-header-collapse" title="Collapse signature" aria-label="Collapse signature" aria-expanded="true"></button>
                        </div>
                        <pre class="shader-draw-chrome shader-draw-header"></pre>
                        <textarea class="shader-editor shader-draw-body" spellcheck="false" readonly></textarea>
                        <pre class="shader-draw-chrome shader-draw-footer"></pre>
                    </div>
                </div>
                <div class="shader-config-workspace hidden">
                    <div class="shader-editor-shell shader-config-shell">
                        <div class="shader-panel-label-row">
                            <div class="shader-panel-label">warning: obviously, editing this can break stuff</div>
                        </div>
                        <textarea class="shader-editor shader-config-editor" spellcheck="false" readonly></textarea>
                    </div>
                </div>
            </div>
        </div>
    `;
}

export function renderShaderRenameIcon() {
    return `
        <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M12 20h9"></path>
            <path d="M16.5 3.5a2.121 2.121 0 1 1 3 3L7 19l-4 1 1-4Z"></path>
        </svg>
    `;
}
