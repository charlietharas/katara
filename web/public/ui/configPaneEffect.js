import { render, SettingsPanelShell, html } from './component.js';
import { ConfigPane, SETTINGS_TAB } from './tabSettingsEffect.js';
import { ShaderPane, SHADER_TAB } from './tabShadersEffect.js';
import { SpecPane, SPEC_TAB, SPEC_URL } from './tabSpecEffect.js';
import { disablePanelKeyboardFocus, isTextEntryElement } from './util.js';
import { PANEL_TABS, TAB_IDS } from './configPaneComponent.js';
import {
    beginSettingsSession,
    cancelSettingsSession,
    clearSettingsSession,
    getSettingsBaseline,
    getSettingsDraft,
    getCommittedConfig,
    loadCommittedConfig,
    replaceCommittedConfig,
} from '../config.js';

const HEADER_ACTION_ICON_RESET = `
    <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <path d="M3 2v6h6"></path>
        <path d="M21 12A9 9 0 0 0 6 5.3L3 8"></path>
        <path d="M21 22v-6h-6"></path>
        <path d="M3 12a9 9 0 0 0 15 6.7l3-2.7"></path>
    </svg>
`;

const HEADER_ACTION_ICON_CONFIRM = `
    <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
        <line x1="12" y1="5" x2="12" y2="14"></line>
        <line x1="12" y1="18" x2="12.01" y2="18"></line>
    </svg>
`;

export class SettingsPanel {
    constructor(parentApp) {
        this.parentApp = parentApp;
        this.panelElement = null;
        this.panelHost = null;
        this.isOpen = false;
        this.wasPausedBeforeOpen = false;
        this.boundKeyHandler = null;
        this.activeTab = SETTINGS_TAB;
        this.clearChangesButton = null;
        this.resetDefaultsButton = null;
        this.saveButton = null;
        this.clearChangesLabel = null;
        this.clearChangesIcon = null;
        this.resetDefaultsLabel = null;
        this.resetDefaultsIcon = null;
        this.saveLabel = null;
        this.resetDefaultsArmed = false;
        this.resetDefaultsTimer = null;
        this.closeInProgress = false;
        this.applyGeneration = 0;

        this.configPane = new ConfigPane(this);
        this.shaderPane = new ShaderPane(this);
        this.specPane = new SpecPane();
    }

    addSection(section) {
        this.configPane.addSection(section);
    }

    finalizeSections() {
        this.configPane.finalizeSections();
    }

    refreshScaleControls() {
        this.configPane.refreshScaleControls();
    }

    switchTab(tab) {
        if (tab === this.activeTab) return;
        if (this.activeTab === SHADER_TAB) {
            this.shaderPane.onLeave();
        }
        if (this.activeTab === SETTINGS_TAB) {
            this.disarmResetDefaults();
        }
        if (tab !== SHADER_TAB) {
            this.shaderPane.disarmClearChanges?.();
        }
        this.activeTab = tab;
        this.panelElement?.classList.remove(
            'settings-panel-tab-settings',
            'settings-panel-tab-shader',
            'settings-panel-tab-spec',
        );
        this.panelElement?.classList.add(`settings-panel-tab-${tab}`);
        this.panelElement?.querySelectorAll('.settings-tab').forEach((button) => {
            button.classList.toggle('selected', button.dataset.tab === tab);
        });
        this.configPane.setVisible(tab === SETTINGS_TAB);
        this.shaderPane.setVisible(tab === SHADER_TAB);
        this.specPane.setVisible(tab === SPEC_TAB);
        this.updateShaderHeaderActions();
        if (tab === SHADER_TAB) {
            this.shaderPane.onShow();
        }
    }

    async open({ tab } = {}) {
        if (this.isOpen) {
            if (tab && TAB_IDS.includes(tab)) {
                this.switchTab(tab);
            }
            this.parentApp.hideShaderErrorOverlay?.();
            return;
        }
        this.wasPausedBeforeOpen = this.parentApp.simulationPaused;
        if (!this.wasPausedBeforeOpen) {
            this.parentApp.togglePause();
        }
        this.saveOriginalConfig();
        // Preserve error-recovery drafts when reopening from the overlay.
        if (!this.parentApp.shaderErrorActive) {
            this.shaderPane.initializeDrafts();
            this.shaderPane.initializeDisplayNames(getSettingsBaseline());
        } else if (!Object.keys(this.shaderPane.drafts || {}).length) {
            this.shaderPane.initializeDrafts();
            this.shaderPane.initializeDisplayNames(getSettingsBaseline());
        }
        if (tab && TAB_IDS.includes(tab)) {
            this.activeTab = tab;
        }
        this.createPanel();
        this.configPane.loadCurrentValues();
        const resolution = getSettingsBaseline()?.simulation?.resolution;
        if (resolution !== undefined) {
            this.configPane.onResolutionPreview(resolution);
        }
        await this.configPane.refreshEnvironmentControls();
        this.isOpen = true;
        this.boundKeyHandler = this.handleKeyDown.bind(this);
        this.attachKeyHandler();
        this.parentApp.hideShaderErrorOverlay?.();
        this.updateShaderHeaderActions();
    }

    attachKeyHandler() {
        if (!this.boundKeyHandler) return;
        document.addEventListener('keydown', this.boundKeyHandler, true);
    }

    detachKeyHandler() {
        if (!this.boundKeyHandler) return;
        document.removeEventListener('keydown', this.boundKeyHandler, true);
    }

    async refreshEnvironmentControls() {
        await this.configPane.refreshEnvironmentControls();
    }

    tearDownPanelDom() {
        if (this.panelHost) {
            render(null, this.panelHost);
            this.panelHost.remove();
        }
        this.panelHost = null;
        this.panelElement = null;
        this.clearChangesButton = null;
        this.resetDefaultsButton = null;
        this.saveButton = null;
        this.clearChangesLabel = null;
        this.clearChangesIcon = null;
        this.resetDefaultsLabel = null;
        this.resetDefaultsIcon = null;
        this.saveLabel = null;
        this.boundKeyHandler = null;
        this.isOpen = false;
        this.closeInProgress = false;
        this.disarmResetDefaults();
        clearSettingsSession();
    }

    disarmResetDefaults() {
        this.resetDefaultsArmed = false;
        if (this.resetDefaultsTimer) {
            clearTimeout(this.resetDefaultsTimer);
            this.resetDefaultsTimer = null;
        }
    }

    handleResetDefaultsClick() {
        if (!this.resetDefaultsArmed) {
            this.resetDefaultsArmed = true;
            this.updateShaderHeaderActions();
            this.resetDefaultsTimer = setTimeout(() => {
                this.resetDefaultsArmed = false;
                this.resetDefaultsTimer = null;
                this.updateShaderHeaderActions();
            }, 3000);
            return;
        }

        this.disarmResetDefaults();
        this.updateShaderHeaderActions();
        void this.resetToDefaultsFromHeader();
    }

    async resetToDefaultsFromHeader() {
        await this.configPane.resetToDefaults();
        if (this.activeTab !== SHADER_TAB || this.shaderPane.workspaceMode !== 'config') {
            return;
        }
        const draft = getSettingsDraft();
        this.shaderPane.syncDisplayNamesFromConfig(draft);
        this.shaderPane.loadConfigEditor();
        this.shaderPane.markConfigDraftPending({ clearConfigError: true });
    }

    bindShaderHeaderActions() {
        this.clearChangesButton = this.panelElement?.querySelector('.settings-btn-clear-changes');
        this.resetDefaultsButton = this.panelElement?.querySelector('.settings-btn-reset-default');
        this.saveButton = this.panelElement?.querySelector('.settings-btn-save');
        this.clearChangesLabel = this.panelElement?.querySelector('.settings-btn-clear-changes-label');
        this.clearChangesIcon = this.panelElement?.querySelector('.settings-btn-clear-changes-icon');
        this.resetDefaultsLabel = this.panelElement?.querySelector('.settings-btn-reset-default-label');
        this.resetDefaultsIcon = this.panelElement?.querySelector('.settings-btn-reset-default-icon');
        this.saveLabel = this.panelElement?.querySelector('.settings-btn-save-label');
        this.clearChangesButton?.addEventListener('click', () => {
            this.shaderPane.handleClearChangesClick();
        });
        this.resetDefaultsButton?.addEventListener('click', () => {
            this.handleResetDefaultsClick();
        });
        this.updateShaderHeaderActions();
    }

    updateShaderHeaderActions({ pulse = false } = {}) {
        const configWorkspace = this.activeTab === SHADER_TAB
            && this.shaderPane.workspaceMode === 'config';
        this.panelElement?.classList.toggle('settings-panel-shader-config', configWorkspace);
        if (configWorkspace) {
            this.shaderPane.disarmClearChanges?.();
        } else if (this.activeTab === SHADER_TAB) {
            this.disarmResetDefaults();
        }

        // Explicit visibility so Clear/Reset can't both end up hidden.
        if (this.clearChangesButton) {
            const showClear = this.activeTab === SHADER_TAB && !configWorkspace;
            this.clearChangesButton.classList.toggle('hidden', !showClear);
            this.clearChangesButton.style.display = showClear ? '' : 'none';
        }
        if (this.resetDefaultsButton) {
            const showReset = this.activeTab === SETTINGS_TAB || configWorkspace;
            this.resetDefaultsButton.classList.toggle('hidden', !showReset);
            this.resetDefaultsButton.style.display = showReset ? 'inline-flex' : 'none';
        }

        const clearArmed = this.shaderPane.clearChangesArmed;
        if (this.clearChangesButton) {
            this.clearChangesButton.classList.toggle('confirm', clearArmed);
            const clearTitle = clearArmed ? 'Confirm clear changes' : 'Clear changes';
            this.clearChangesButton.title = clearTitle;
            this.clearChangesButton.setAttribute('aria-label', clearTitle);
            if (this.clearChangesLabel) {
                this.clearChangesLabel.textContent = clearArmed ? 'CONFIRM?' : 'CLEAR CHANGES';
            }
            if (this.clearChangesIcon) {
                this.clearChangesIcon.innerHTML = clearArmed
                    ? HEADER_ACTION_ICON_CONFIRM
                    : HEADER_ACTION_ICON_RESET;
            }
        }

        if (this.resetDefaultsButton) {
            const resetArmed = this.resetDefaultsArmed;
            this.resetDefaultsButton.classList.toggle('confirm', resetArmed);
            const resetTitle = resetArmed ? 'Confirm reset to default' : 'Reset to default';
            this.resetDefaultsButton.title = resetTitle;
            this.resetDefaultsButton.setAttribute('aria-label', resetTitle);
            if (this.resetDefaultsLabel) {
                this.resetDefaultsLabel.textContent = resetArmed ? 'CONFIRM?' : 'RESET TO DEFAULT';
            }
            if (this.resetDefaultsIcon) {
                this.resetDefaultsIcon.innerHTML = resetArmed
                    ? HEADER_ACTION_ICON_CONFIRM
                    : HEADER_ACTION_ICON_RESET;
            }
        }

        if (!this.saveButton) return;

        const showErrors = this.shaderPane.compileErrorActive;
        const configError = showErrors && this.shaderPane.errorKind === 'config';
        const showDestructive = !showErrors && !!this.shaderPane.configDraftPending;
        const showApply = !showErrors && (this.shaderPane.hasUnappliedChanges() || showDestructive);
        this.saveButton.classList.toggle('pending', showApply);
        this.saveButton.classList.toggle('destructive', showDestructive);
        this.saveButton.classList.toggle('error', showErrors);
        if (this.saveLabel) {
            if (configError) {
                this.saveLabel.textContent = 'CONFIG ERROR';
            } else if (showErrors) {
                this.saveLabel.textContent = 'SHADER ERRORS';
            } else if (showApply) {
                this.saveLabel.textContent = 'ENTER TO APPLY';
            } else {
                this.saveLabel.textContent = '[ENTER]';
            }
        }
        const saveTitle = configError
            ? 'Fix config errors'
            : showErrors
                ? 'Fix shader errors'
                : showDestructive
                    ? 'Apply config and shader changes'
                    : showApply
                        ? 'Apply shader changes'
                        : 'Save';
        this.saveButton.title = saveTitle;
        this.saveButton.setAttribute('aria-label', saveTitle);

        if (pulse && (showApply || showErrors)) {
            this.pulseEnterApplyButton();
        }
    }

    pulseEnterApplyButton() {
        if (!this.saveButton) return;
        this.saveButton.classList.remove('pulse');
        void this.saveButton.offsetWidth;
        this.saveButton.classList.add('pulse');
        const onEnd = () => {
            this.saveButton?.classList.remove('pulse');
            this.saveButton?.removeEventListener('animationend', onEnd);
        };
        this.saveButton.addEventListener('animationend', onEnd);
    }

    async close(saveChanges = false) {
        if (!this.isOpen) return;

        // Discard always wins — invalidate any in-flight Enter apply.
        if (!saveChanges) {
            this.applyGeneration += 1;
            this.closeInProgress = false;
            this.detachKeyHandler();
            const hadShaderError = this.parentApp.shaderErrorActive;
            if (!hadShaderError) {
                this.shaderPane.resetDrafts();
                cancelSettingsSession();
                const committed = getCommittedConfig();
                if (committed) {
                    this.parentApp.mergeViewportTargetsIntoConfig?.(committed);
                    replaceCommittedConfig(committed);
                }
            }

            this.tearDownPanelDom();

            if (hadShaderError) {
                this.parentApp.showShaderErrorOverlay?.(this.shaderPane.lastErrorMessage);
                return;
            }

            if (!this.wasPausedBeforeOpen && this.parentApp.simulationPaused) {
                this.parentApp.togglePause();
            }
            return;
        }

        if (this.closeInProgress) return;
        this.closeInProgress = true;
        const applyGeneration = ++this.applyGeneration;

        try {
            // Keep Escape/Cancel live during apply — only detach on teardown.
            const result = await this.configPane.saveAndApply();
            if (applyGeneration !== this.applyGeneration || !this.isOpen) {
                return;
            }
            if (result === 'error') {
                return;
            }
            if (result === 'reload') {
                return;
            }

            this.detachKeyHandler();
            if (result === 'shader') {
                const message = this.shaderPane.lastErrorMessage || 'Shader compilation failed';
                const resumeAfterFix = !this.wasPausedBeforeOpen;
                this.tearDownPanelDom();
                this.parentApp.handleShaderCompileFailure?.(message, { resumeAfterFix });
                return;
            }

            this.tearDownPanelDom();
            if (this.parentApp.shaderErrorActive) {
                this.parentApp.clearShaderCompileFailure?.({ resume: true });
            } else if (!this.wasPausedBeforeOpen && this.parentApp.simulationPaused) {
                this.parentApp.togglePause();
            }
        } finally {
            if (applyGeneration === this.applyGeneration) {
                this.closeInProgress = false;
            }
        }
    }

    createPanel() {
        const host = document.createElement('div');
        document.body.appendChild(host);
        this.panelHost = host;

        this.renderPanel((body) => {
            this.configPane.mount(body);
            this.shaderPane.mount(body);
            this.specPane.mount(body);

            const panel = host.firstElementChild;
            if (panel) {
                this.panelElement = panel;
                panel.tabIndex = -1;
                setTimeout(() => panel.focus(), 0);
                disablePanelKeyboardFocus(panel);
                this.bindShaderHeaderActions();
            }

            const desiredTab = this.activeTab;
            this.activeTab = '__init__';
            this.switchTab(desiredTab);
        });
    }

    renderPanel(onBodyReady) {
        if (!this.panelHost) return;
        render(html`
            <${SettingsPanelShell}
                activeTab=${this.activeTab}
                tabs=${PANEL_TABS}
                specOpenUrl=${SPEC_URL}
                onResetDefaults=${true}
                onBodyReady=${onBodyReady}
                onTabChange=${(tab) => this.switchTab(tab)}
                onCancel=${() => void this.close(false)}
                onSave=${() => void this.close(true)}
            />
        `, this.panelHost);
    }

    handleKeyDown(e) {
        if (!this.isOpen || !this.panelElement) return;
        if (this.shaderPane.handleKeyDown(e)) return;

        if (e.key === 'Escape') {
            e.preventDefault();
            e.stopPropagation();
            void this.close(false);
            return;
        }

        if (e.key === 'Enter') {
            if (isTextEntryElement(e.target)) {
                return;
            }
            e.preventDefault();
            e.stopPropagation();
            void this.close(true);
        }
    }

    async refresh() {
        if (!this.isOpen) return;
        this.saveOriginalConfig();
        this.configPane.loadCurrentValues();
        await this.configPane.refreshEnvironmentControls();
    }

    saveOriginalConfig() {
        beginSettingsSession(loadCommittedConfig() ?? getCommittedConfig());
    }
}
