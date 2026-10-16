import { h, render } from 'preact';
import { useEffect, useRef } from 'preact/hooks';
import htm from 'htm';

export const html = htm.bind(h);
export { h, render };

export function mountComponent(component, container) {
    render(component, container);
    return container;
}

export function unmountComponent(container) {
    render(null, container);
}

export function formatSliderValue(value, step) {
    const stepText = String(step);
    const dot = stepText.indexOf('.');
    const decimals = dot === -1 ? 0 : stepText.length - dot - 1;
    if (decimals === 0) return String(Math.round(value));
    return value.toFixed(decimals);
}

function preventCheckboxEnterDefault(event) {
    if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
    }
}

export function SliderRow({
    label,
    value,
    min,
    max,
    step,
    disabled = false,
    onChange,
    onLiveChange,
}) {
    const format = (next) => formatSliderValue(next, step);

    return html`
        <div class="config-row ${disabled ? 'config-row-disabled' : ''}">
            <label>${label}</label>
            <input
                type="range"
                min=${min}
                max=${max}
                step=${step}
                value=${value}
                disabled=${disabled}
                onInput=${(event) => {
                    const next = parseFloat(event.currentTarget.value);
                    onChange?.(next);
                    onLiveChange?.(next);
                }}
            />
            <span class="value-display">${format(value)}</span>
        </div>
    `;
}

export function CheckboxRow({ label, checked, disabled = false, onChange }) {
    return html`
        <div class="config-row checkbox-row ${disabled ? 'config-row-disabled' : ''}">
            <label>
                <input
                    type="checkbox"
                    checked=${checked}
                    disabled=${disabled}
                    onKeyDown=${preventCheckboxEnterDefault}
                    onChange=${(event) => onChange?.(event.currentTarget.checked)}
                />
                ${label}
            </label>
        </div>
    `;
}

export function HeaderCheckbox({ label, checked, disabled = false, onChange }) {
    return html`
        <label class="config-header-toggle ${disabled ? 'config-header-toggle-disabled' : ''}">
            <input
                type="checkbox"
                checked=${checked}
                disabled=${disabled}
                onKeyDown=${preventCheckboxEnterDefault}
                onChange=${(event) => onChange?.(event.currentTarget.checked)}
            />
            <span>${label}</span>
        </label>
    `;
}

export function ModeButtonGroup({ modes, value, disabledModes = [], onChange }) {
    return html`
        <div class="input-mode-control">
            <div class="input-mode-options">
                ${modes.map((mode) => {
                    const disabled = disabledModes.includes(mode.value);
                    return html`
                        <button
                            type="button"
                            class="input-mode-btn ${value === mode.value ? 'selected' : ''} ${disabled ? 'input-mode-btn-disabled' : ''}"
                            data-mode=${mode.value}
                            disabled=${disabled}
                            onClick=${() => onChange?.(mode.value)}
                        >
                            ${mode.label}
                        </button>
                    `;
                })}
            </div>
        </div>
    `;
}

export function ConfigSectionView({
    title,
    layout = 'column',
    disabled = false,
    headerControls = [],
    controls = [],
}) {
    const contentClass = layout === 'grid4'
        ? 'config-grid config-grid-4'
        : layout === 'grid2'
            ? 'config-grid'
            : layout === 'canvasRow'
                ? 'config-canvas-row'
                : '';

    return html`
        <div class="config-section ${disabled ? 'config-section-disabled' : ''}">
            <div class="config-section-header">
                <h3>${title}</h3>
                <div class="config-section-header-controls">${headerControls}</div>
            </div>
            <div class="config-section-content">
                ${contentClass
                    ? html`<div class=${contentClass}>${controls}</div>`
                    : controls}
            </div>
        </div>
    `;
}

export function SettingsPanelShell({
    activeTab,
    tabs,
    onTabChange,
    onCancel,
    onSave,
    onBodyReady,
    specOpenUrl,
    onResetDefaults,
}) {
    const bodyReadyRef = useRef(false);

    const bodyRef = (element) => {
        if (!element || bodyReadyRef.current) return;
        bodyReadyRef.current = true;
        onBodyReady?.(element);
    };

    return html`
        <div class="settings-panel" tabIndex="-1">
            <div class="settings-panel-header">
                <div class="settings-panel-tabs">
                    ${tabs.map((tab) => html`
                        <button
                            type="button"
                            class="input-mode-btn settings-tab ${tab.id === 'shader' ? 'settings-tab-shader' : ''} ${tab.id === activeTab ? 'selected' : ''}"
                            data-tab=${tab.id}
                            onClick=${() => onTabChange(tab.id)}
                        >
                            ${tab.label}
                        </button>
                    `)}
                </div>
                <div class="settings-panel-buttons">
                    ${onResetDefaults ? html`
                        <button
                            type="button"
                            class="input-mode-btn settings-btn-reset-default"
                            title="Reset to default"
                            aria-label="Reset to default"
                        >
                            <span class="settings-btn-reset-default-icon" aria-hidden="true">
                                <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                                    <path d="M3 2v6h6"></path>
                                    <path d="M21 12A9 9 0 0 0 6 5.3L3 8"></path>
                                    <path d="M21 22v-6h-6"></path>
                                    <path d="M3 12a9 9 0 0 0 15 6.7l3-2.7"></path>
                                </svg>
                            </span>
                            <span class="settings-btn-reset-default-label">RESET TO DEFAULT</span>
                        </button>
                    ` : null}
                    <button
                        type="button"
                        class="input-mode-btn settings-btn-clear-changes"
                        title="Clear changes"
                        aria-label="Clear changes"
                    >
                        <span class="settings-btn-clear-changes-icon" aria-hidden="true">
                            <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                                <path d="M3 2v6h6"></path>
                                <path d="M21 12A9 9 0 0 0 6 5.3L3 8"></path>
                                <path d="M21 22v-6h-6"></path>
                                <path d="M3 12a9 9 0 0 0 15 6.7l3-2.7"></path>
                            </svg>
                        </span>
                        <span class="settings-btn-clear-changes-label">CLEAR CHANGES</span>
                    </button>
                    ${specOpenUrl ? html`
                        <a
                            class="input-mode-btn settings-btn-spec-open"
                            href=${specOpenUrl}
                            target="_blank"
                            rel="noopener noreferrer"
                            title="View in separate tab"
                            aria-label="View in separate tab"
                        >
                            <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                                <path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"></path>
                                <polyline points="15 3 21 3 21 9"></polyline>
                                <line x1="10" y1="14" x2="21" y2="3"></line>
                            </svg>
                            <span class="settings-btn-spec-open-label">VIEW IN SEPARATE TAB</span>
                        </a>
                    ` : null}
                    <button class="input-mode-btn settings-btn-cancel" title="Discard" aria-label="Discard" onClick=${onCancel}>
                        <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                            <line x1="18" y1="6" x2="6" y2="18"></line>
                            <line x1="6" y1="6" x2="18" y2="18"></line>
                        </svg>
                        <span class="settings-btn-cancel-label">[ESC]</span>
                    </button>
                    <button class="input-mode-btn settings-btn-save" title="Save" aria-label="Save" onClick=${onSave}>
                        <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                            <polyline points="20,6 9,17 4,12"></polyline>
                        </svg>
                        <span class="settings-btn-save-label">[ENTER]</span>
                    </button>
                </div>
            </div>
            <div class="settings-panel-body" ref=${bodyRef}></div>
        </div>
    `;
}

export function ImperativeControlHost({ renderControl, deps = [] }) {
    const hostRef = useRef(null);

    useEffect(() => {
        const host = hostRef.current;
        if (!host) return undefined;
        host.replaceChildren();
        const element = renderControl();
        if (element) host.appendChild(element);
        return () => host.replaceChildren();
    }, deps);

    return html`<div ref=${hostRef}></div>`;
}
