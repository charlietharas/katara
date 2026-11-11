import {
    getViewTargetCount,
    formatTargetIndex,
    getBuiltinShaderDisplayName,
    disablePanelKeyboardFocus,
} from './util.js';
import {
    readCommittedConfig,
    replaceCommittedConfig,
    writeConfigToFs,
    getSettingsBaseline,
    getSettingsDraft,
    replaceSettingsDraft,
} from '../config.js';
import {
    SHADER_TAB,
    renderShaderWorkspaceShell,
    renderShaderRenameIcon,
    renderShaderDrawCollapseIcon,
    splitViewSource,
    joinViewSource,
    VIEW_DRAW_OPEN,
    VIEW_DRAW_OPEN_COLLAPSED,
    VIEW_DRAW_CLOSE,
    HELPERS_EMPTY_PLACEHOLDER,
} from './tabShadersComponent.js';

export { SHADER_TAB };

export class ShaderPane {
    constructor(panel) {
        this.panel = panel;
        this.root = null;
        this.targetIndex = 0;
        this.workspaceMode = 'view';
        this.drafts = {};
        this.baselineDrafts = {};
        this.displayNameDrafts = {};
        this.baselineDisplayNames = {};
        this.renamingIndex = null;
        this.editorEditing = false;
        this.editorSessionHelpers = '';
        this.editorSessionBody = '';
        this.pendingApply = false;
        this.configDraftPending = false;
        this.compileErrorActive = false;
        this.errorKind = null;
        this.lastErrorMessage = '';
        this.editorSessionConfig = '';
        this.clearChangesArmed = false;
        this.clearChangesTimer = null;
        this.explorerElement = null;
        this.editorTitleElement = null;
        this.editorEditButton = null;
        this.editorSaveButton = null;
        this.editorCancelButton = null;
        this.viewPanelsElement = null;
        this.configWorkspaceElement = null;
        this.configShellElement = null;
        this.configEditorElement = null;
        this.helpersShellElement = null;
        this.drawShellElement = null;
        this.helpersEditorElement = null;
        this.drawBodyElement = null;
        this.drawHeaderElement = null;
        this.drawFooterElement = null;
        this.drawHeaderCollapseButton = null;
        this.drawHeaderCollapsed = false;
        this.helpersShowingPlaceholder = false;
        this.explorerListElement = null;
        this.explorerPinElement = null;
    }

    mount(parent) {
        this.root = document.createElement('div');
        this.root.className = 'settings-panel-content settings-tab-pane hidden shader-editor-pane';
        this.root.dataset.tab = SHADER_TAB;
        this.root.innerHTML = renderShaderWorkspaceShell();
        parent.appendChild(this.root);

        this.explorerElement = this.root.querySelector('.shader-explorer');
        this.explorerListElement = this.root.querySelector('.shader-explorer-list');
        this.explorerPinElement = this.root.querySelector('.shader-explorer-pin');
        this.editorTitleElement = this.root.querySelector('.shader-editor-title');
        this.editorEditButton = this.root.querySelector('.shader-editor-edit');
        this.editorSaveButton = this.root.querySelector('.shader-editor-save');
        this.editorCancelButton = this.root.querySelector('.shader-editor-cancel');
        this.viewPanelsElement = this.root.querySelector('.shader-view-panels');
        this.configWorkspaceElement = this.root.querySelector('.shader-config-workspace');
        this.configShellElement = this.root.querySelector('.shader-config-shell');
        this.configEditorElement = this.root.querySelector('.shader-config-editor');
        this.helpersShellElement = this.root.querySelector('.shader-helpers-shell');
        this.drawShellElement = this.root.querySelector('.shader-draw-shell');
        this.helpersEditorElement = this.root.querySelector('.shader-helpers-editor');
        this.drawBodyElement = this.root.querySelector('.shader-draw-body');
        this.drawHeaderElement = this.root.querySelector('.shader-draw-header');
        this.drawFooterElement = this.root.querySelector('.shader-draw-footer');
        this.drawHeaderCollapseButton = this.root.querySelector('.shader-draw-header-collapse');

        if (this.drawFooterElement) {
            this.drawFooterElement.textContent = VIEW_DRAW_CLOSE;
        }
        this.drawHeaderCollapseButton?.addEventListener('click', () => {
            this.drawHeaderCollapsed = !this.drawHeaderCollapsed;
            this.updateDrawHeaderChrome();
        });
        this.updateDrawHeaderChrome();

        this.editorEditButton.addEventListener('click', () => this.startEditorEdit());
        this.editorSaveButton.addEventListener('click', () => this.saveEditorEdit());
        this.editorCancelButton.addEventListener('click', () => this.cancelEditorEdit());
        this.bindEditorInteractionEvents(this.helpersEditorElement);
        this.bindEditorInteractionEvents(this.drawBodyElement);
        this.bindEditorInteractionEvents(this.configEditorElement);
        this.updateWorkspaceVisibility();
        this.updateEditorChrome();
        this.updateEditorTitle();
        this.renderExplorer();
        return this.root;
    }

    setVisible(visible) {
        this.root?.classList.toggle('hidden', !visible);
    }

    onShow() {
        if (this.workspaceMode === 'view') {
            this.loadEditorForTarget(this.targetIndex);
        } else if (this.workspaceMode === 'config') {
            this.loadConfigEditor();
        } else {
            this.setWorkspaceMode(this.workspaceMode);
        }
        this.panel.updateShaderHeaderActions?.();
    }

    onLeave() {
        if (this.editorEditing) {
            this.cancelEditorEdit();
        } else if (this.workspaceMode === 'view') {
            this.flushEditorDraft();
        }
    }

    resetDrafts() {
        if (this.editorEditing) {
            this.cancelEditorEdit();
        }
        this.drafts = { ...this.baselineDrafts };
        this.displayNameDrafts = { ...this.baselineDisplayNames };
        this.renamingIndex = null;
        this.pendingApply = false;
        this.configDraftPending = false;
        this.disarmClearChanges();
        if (this.workspaceMode === 'view') {
            this.loadEditorForTarget(this.targetIndex);
        } else if (this.workspaceMode === 'config') {
            this.loadConfigEditor();
        } else {
            this.setWorkspaceMode(this.workspaceMode);
        }
        this.panel.updateShaderHeaderActions?.();
    }

    readSourceFromWasm(index) {
        const mod = this.panel.parentApp.module;
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

    initializeDisplayNames(originalConfig) {
        this.displayNameDrafts = {};
        this.baselineDisplayNames = {};
        for (let index = 0; index < getViewTargetCount(); index++) {
            const key = String(index);
            const customName = originalConfig?.customViews?.[key]?.displayName
                ?? readCommittedConfig()?.customViews?.[key]?.displayName;
            const name = customName || getBuiltinShaderDisplayName(index);
            this.displayNameDrafts[index] = name;
            this.baselineDisplayNames[index] = name;
        }
    }

    getDisplayName(index) {
        return this.displayNameDrafts[index] ?? getBuiltinShaderDisplayName(index);
    }

    getExplorerFilename(index) {
        const slug = this.getDisplayName(index).toLowerCase().replace(/[\s.-]+/g, '_').replace(/_+/g, '_');
        return `${formatTargetIndex(index)}_${slug}.wgsl`;
    }

    updateEditorTitle() {
        if (!this.editorTitleElement) return;
        if (this.workspaceMode === 'config') {
            this.editorTitleElement.textContent = 'CONFIG';
            return;
        }
        this.editorTitleElement.textContent = this.getDisplayName(this.targetIndex);
    }

    setWorkspaceMode(mode) {
        if (this.editorEditing) {
            this.cancelEditorEdit();
        } else if (this.workspaceMode === 'view' && mode !== 'view') {
            this.flushEditorDraft();
        }
        this.workspaceMode = mode;
        if (mode === 'config') {
            this.loadConfigEditor();
        }
        this.updateWorkspaceVisibility();
        this.updateEditorChrome();
        this.updateEditorTitle();
        this.renderExplorer();
        this.panel.updateShaderHeaderActions?.();
    }

    formatConfigDraft() {
        const draft = getSettingsDraft();
        return `${JSON.stringify(draft ?? {}, null, 4)}\n`;
    }

    loadConfigEditor() {
        if (!this.configEditorElement) return;
        this.configEditorElement.value = this.formatConfigDraft();
        this.editorEditing = false;
        this.editorSessionConfig = '';
        this.updateWorkspaceVisibility();
        this.updateEditorChrome();
        this.updateEditorTitle();
    }

    syncDisplayNamesFromConfig(config) {
        for (let index = 0; index < getViewTargetCount(); index++) {
            const key = String(index);
            const customName = config?.customViews?.[key]?.displayName;
            this.displayNameDrafts[index] = customName || getBuiltinShaderDisplayName(index);
        }
        this.renderExplorer();
        this.updateEditorTitle();
    }

    updateWorkspaceVisibility() {
        const mode = this.workspaceMode;
        this.viewPanelsElement?.classList.toggle('hidden', mode !== 'view');
        this.configWorkspaceElement?.classList.toggle('hidden', mode !== 'config');
    }

    updateDrawHeaderChrome() {
        const collapsed = this.drawHeaderCollapsed;
        if (this.drawHeaderElement) {
            this.drawHeaderElement.textContent = collapsed
                ? VIEW_DRAW_OPEN_COLLAPSED
                : VIEW_DRAW_OPEN.replace(/\n$/, '');
            this.drawHeaderElement.classList.toggle('collapsed', collapsed);
        }
        if (this.drawHeaderCollapseButton) {
            this.drawHeaderCollapseButton.innerHTML = renderShaderDrawCollapseIcon(collapsed);
            this.drawHeaderCollapseButton.title = collapsed ? 'Expand signature' : 'Collapse signature';
            this.drawHeaderCollapseButton.setAttribute('aria-label', collapsed ? 'Expand signature' : 'Collapse signature');
            this.drawHeaderCollapseButton.setAttribute('aria-expanded', collapsed ? 'false' : 'true');
            this.drawHeaderCollapseButton.classList.toggle('collapsed', collapsed);
        }
    }

    renderExplorer() {
        if (!this.explorerListElement || !this.explorerPinElement) return;

        this.explorerListElement.innerHTML = '';
        for (let index = 0; index < getViewTargetCount(); index++) {
            this.explorerListElement.appendChild(this.buildViewExplorerItem(index));
        }

        this.explorerPinElement.innerHTML = '';
        this.explorerPinElement.appendChild(this.buildDeveloperExplorerItem('config.json'));

        disablePanelKeyboardFocus(this.panel.panelElement);
        this.updateEditorTitle();
    }

    buildViewExplorerItem(index) {
        const item = document.createElement('div');
        item.className = 'shader-explorer-item';
        if (this.workspaceMode === 'view' && index === this.targetIndex) {
            item.classList.add('selected');
        }
        if (this.renamingIndex === index) {
            item.classList.add('renaming');
            const prefix = document.createElement('span');
            prefix.className = 'shader-explorer-prefix';
            prefix.textContent = `${formatTargetIndex(index)}_`;
            const input = document.createElement('input');
            input.type = 'text';
            input.className = 'shader-explorer-rename-input';
            input.dataset.index = String(index);
            input.spellcheck = false;
            input.value = this.getDisplayName(index);
            const suffix = document.createElement('span');
            suffix.className = 'shader-explorer-suffix';
            suffix.textContent = '.wgsl';
            input.addEventListener('keydown', (event) => {
                event.stopPropagation();
                if (event.key === 'Enter') {
                    event.preventDefault();
                    this.commitRename(index, input.value);
                } else if (event.key === 'Escape') {
                    event.preventDefault();
                    this.cancelRename();
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
            selectBtn.textContent = this.getExplorerFilename(index);
            selectBtn.addEventListener('click', () => {
                this.loadEditorForTarget(index);
            });
            item.appendChild(selectBtn);

            const renameBtn = document.createElement('button');
            renameBtn.type = 'button';
            renameBtn.className = 'shader-explorer-rename';
            renameBtn.title = 'Rename';
            renameBtn.innerHTML = renderShaderRenameIcon();
            renameBtn.addEventListener('click', (event) => {
                event.stopPropagation();
                this.startRename(index);
            });
            item.appendChild(renameBtn);
        }
        return item;
    }

    buildDeveloperExplorerItem(label, mode = 'config') {
        const item = document.createElement('div');
        item.className = 'shader-explorer-item';
        if (this.workspaceMode === mode) {
            item.classList.add('selected');
        }

        const selectBtn = document.createElement('button');
        selectBtn.type = 'button';
        selectBtn.className = 'shader-explorer-select';
        selectBtn.textContent = label;
        selectBtn.addEventListener('click', () => {
            this.setWorkspaceMode(mode);
        });
        item.appendChild(selectBtn);
        return item;
    }

    markPendingApply({ clearCompileError = false } = {}) {
        if (clearCompileError && this.compileErrorActive) {
            this.compileErrorActive = false;
            this.errorKind = null;
        }
        this.pendingApply = true;
        this.panel.updateShaderHeaderActions?.({ pulse: true });
    }

    markConfigDraftPending({ clearConfigError = false } = {}) {
        if (clearConfigError && this.compileErrorActive && this.errorKind === 'config') {
            this.compileErrorActive = false;
            this.errorKind = null;
            this.lastErrorMessage = '';
        }
        this.configDraftPending = true;
        this.panel.updateShaderHeaderActions?.({ pulse: true });
    }

    startRename(index) {
        this.renamingIndex = index;
        this.renderExplorer();
    }

    commitRename(index, rawValue) {
        const trimmed = rawValue.trim();
        if (trimmed) {
            const next = trimmed.toUpperCase().replace(/[\s.-]+/g, '_').replace(/_+/g, '_');
            this.displayNameDrafts[index] = next;
            if (next !== (this.baselineDisplayNames[index] ?? '')) {
                this.markPendingApply({ clearCompileError: true });
            }
        }
        this.renamingIndex = null;
        this.renderExplorer();
        this.updateEditorTitle();
        this.panel.updateShaderHeaderActions?.();
    }

    cancelRename() {
        this.renamingIndex = null;
        this.renderExplorer();
    }

    initializeDrafts() {
        this.drafts = {};
        this.baselineDrafts = {};
        for (let index = 0; index < getViewTargetCount(); index++) {
            const source = this.readSourceFromWasm(index);
            this.drafts[index] = source;
            this.baselineDrafts[index] = source;
        }
        this.pendingApply = false;
        this.configDraftPending = false;
        this.disarmClearChanges();
    }

    getEditorParts() {
        return {
            helpers: this.getHelpersEditorValue(),
            body: this.drawBodyElement?.value ?? '',
        };
    }

    getHelpersEditorValue() {
        if (!this.helpersEditorElement || this.helpersShowingPlaceholder) {
            return '';
        }
        return this.helpersEditorElement.value;
    }

    setHelpersEditorValue(helpers, { allowPlaceholder = true } = {}) {
        if (!this.helpersEditorElement) return;
        const trimmed = (helpers ?? '').replace(/\s+$/, '');
        if (allowPlaceholder && !this.editorEditing && trimmed.length === 0) {
            this.helpersEditorElement.value = HELPERS_EMPTY_PLACEHOLDER;
            this.helpersShowingPlaceholder = true;
            this.helpersEditorElement.classList.add('placeholder');
            return;
        }
        this.helpersEditorElement.value = helpers ?? '';
        this.helpersShowingPlaceholder = false;
        this.helpersEditorElement.classList.remove('placeholder');
    }

    clearHelpersPlaceholderForEdit() {
        if (!this.helpersShowingPlaceholder || !this.helpersEditorElement) return;
        this.helpersEditorElement.value = '';
        this.helpersShowingPlaceholder = false;
        this.helpersEditorElement.classList.remove('placeholder');
    }

    setEditorParts(source) {
        const parts = splitViewSource(source);
        this.setHelpersEditorValue(parts.helpers);
        if (this.drawBodyElement) {
            this.drawBodyElement.value = parts.body;
        }
    }

    composeEditorDraft() {
        const { helpers, body } = this.getEditorParts();
        return joinViewSource(helpers, body);
    }

    flushEditorDraft() {
        if (!this.helpersEditorElement || !this.drawBodyElement) return;
        if (this.helpersEditorElement.readOnly) return;
        this.drafts[this.targetIndex] = this.composeEditorDraft();
    }

    updateEditorChrome() {
        const editing = this.editorEditing;
        const viewMode = this.workspaceMode === 'view';
        const configMode = this.workspaceMode === 'config';
        const editorMode = viewMode || configMode;
        if (this.editorEditButton) {
            this.editorEditButton.classList.toggle('hidden', !editorMode || editing);
            this.editorEditButton.disabled = !editorMode;
            this.editorEditButton.classList.toggle('input-mode-btn-disabled', !editorMode);
        }
        if (this.editorSaveButton) {
            this.editorSaveButton.classList.toggle('hidden', !editorMode || !editing);
        }
        if (this.editorCancelButton) {
            this.editorCancelButton.classList.toggle('hidden', !editorMode || !editing);
        }
        for (const shell of [this.helpersShellElement, this.drawShellElement]) {
            shell?.classList.toggle('editing', viewMode && editing);
        }
        this.configShellElement?.classList.toggle('editing', configMode && editing);
        for (const editor of [this.helpersEditorElement, this.drawBodyElement]) {
            if (!editor) continue;
            editor.readOnly = !viewMode || !editing;
            if (viewMode && editing) {
                editor.removeAttribute('readonly');
            } else {
                editor.setAttribute('readonly', '');
            }
        }
        if (this.configEditorElement) {
            this.configEditorElement.readOnly = !configMode || !editing;
            if (configMode && editing) {
                this.configEditorElement.removeAttribute('readonly');
            } else {
                this.configEditorElement.setAttribute('readonly', '');
            }
        }
        if (viewMode && !editing) {
            this.setHelpersEditorValue(this.getHelpersEditorValue());
        }
    }

    bindEditorInteractionEvents(editorElement) {
        if (!editorElement) return;

        const stopWhenEditing = (event) => {
            if (this.editorEditing) {
                event.stopPropagation();
            }
        };

        for (const eventName of ['keydown', 'keypress', 'keyup', 'input', 'beforeinput', 'mousedown', 'click', 'paste']) {
            editorElement.addEventListener(eventName, stopWhenEditing);
        }

        editorElement.addEventListener('keydown', (event) => {
            if (!this.editorEditing || event.key !== 'Tab') return;
            event.preventDefault();
            event.stopPropagation();
            const start = editorElement.selectionStart ?? 0;
            const end = editorElement.selectionEnd ?? start;
            const value = editorElement.value;
            const insert = '    ';
            editorElement.value = `${value.slice(0, start)}${insert}${value.slice(end)}`;
            const next = start + insert.length;
            editorElement.setSelectionRange(next, next);
        });

        editorElement.addEventListener('mousedown', () => {
            if (this.editorEditing) {
                editorElement.focus();
            }
        });

        if (editorElement === this.helpersEditorElement) {
            editorElement.addEventListener('focus', () => {
                if (this.editorEditing) {
                    this.clearHelpersPlaceholderForEdit();
                }
            });
        }
    }

    focusEditor() {
        if (!this.editorEditing) return;
        if (this.workspaceMode === 'config') {
            if (!this.configEditorElement) return;
            this.configEditorElement.focus();
            const length = this.configEditorElement.value.length;
            this.configEditorElement.setSelectionRange(length, length);
            return;
        }
        if (!this.drawBodyElement) return;
        this.drawBodyElement.focus();
        const length = this.drawBodyElement.value.length;
        this.drawBodyElement.setSelectionRange(length, length);
    }

    startEditorEdit() {
        if (this.workspaceMode === 'config') {
            if (!this.configEditorElement) return;
            this.editorSessionConfig = this.configEditorElement.value;
            this.editorEditing = true;
            this.updateEditorChrome();
            requestAnimationFrame(() => this.focusEditor());
            return;
        }
        if (this.workspaceMode !== 'view') return;
        if (!this.helpersEditorElement || !this.drawBodyElement) return;
        this.clearHelpersPlaceholderForEdit();
        this.editorSessionHelpers = this.helpersEditorElement.value;
        this.editorSessionBody = this.drawBodyElement.value;
        this.editorEditing = true;
        this.updateEditorChrome();
        requestAnimationFrame(() => this.focusEditor());
    }

    saveEditorEdit() {
        if (!this.editorEditing) return true;
        if (this.workspaceMode === 'config') {
            return this.saveConfigEditorEdit();
        }
        this.drafts[this.targetIndex] = this.composeEditorDraft();
        this.editorEditing = false;
        this.editorSessionHelpers = '';
        this.editorSessionBody = '';
        this.updateEditorChrome();
        this.setHelpersEditorValue(this.getHelpersEditorValue());

        const hadError = this.compileErrorActive && this.errorKind !== 'config';
        if (hadError || this.draftsChanged() || this.displayNamesChanged()) {
            this.markPendingApply({ clearCompileError: hadError });
        } else {
            this.panel.updateShaderHeaderActions?.();
        }
        return true;
    }

    saveConfigEditorEdit() {
        let parsed;
        try {
            parsed = JSON.parse(this.configEditorElement?.value ?? '');
        } catch (_) {
            alert('Invalid JSON — fix the syntax before saving.');
            return false;
        }
        if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
            alert('Config must be a JSON object.');
            return false;
        }

        replaceSettingsDraft(parsed);
        this.syncDisplayNamesFromConfig(parsed);
        this.panel.configPane?.loadValuesFromConfig?.(parsed, { syncDraft: false });
        this.configEditorElement.value = this.formatConfigDraft();
        this.editorEditing = false;
        this.editorSessionConfig = '';
        this.updateEditorChrome();
        this.markConfigDraftPending({ clearConfigError: true });
        return true;
    }

    cancelEditorEdit() {
        if (!this.editorEditing) return;
        if (this.workspaceMode === 'config') {
            if (this.configEditorElement) {
                this.configEditorElement.value = this.editorSessionConfig;
            }
            this.editorEditing = false;
            this.editorSessionConfig = '';
            this.updateEditorChrome();
            return;
        }
        if (!this.helpersEditorElement || !this.drawBodyElement) return;
        this.helpersEditorElement.value = this.editorSessionHelpers;
        this.drawBodyElement.value = this.editorSessionBody;
        this.helpersShowingPlaceholder = false;
        this.helpersEditorElement.classList.remove('placeholder');
        this.editorEditing = false;
        this.editorSessionHelpers = '';
        this.editorSessionBody = '';
        this.updateEditorChrome();
        this.setHelpersEditorValue(this.helpersEditorElement.value);
    }

    loadEditorForTarget(index) {
        if (this.editorEditing) {
            this.cancelEditorEdit();
        }
        this.workspaceMode = 'view';
        this.targetIndex = index;
        const source = this.drafts[index] ?? this.readSourceFromWasm(index);
        this.setEditorParts(source);
        this.editorEditing = false;
        this.editorSessionHelpers = '';
        this.editorSessionBody = '';
        this.updateWorkspaceVisibility();
        this.updateEditorChrome();
        this.updateEditorTitle();
        this.renderExplorer();
        this.panel.updateShaderHeaderActions?.();
    }

    draftsChanged() {
        for (let index = 0; index < getViewTargetCount(); index++) {
            if ((this.drafts[index] ?? '') !== (this.baselineDrafts[index] ?? '')) {
                return true;
            }
        }
        return false;
    }

    displayNamesChanged() {
        for (let index = 0; index < getViewTargetCount(); index++) {
            if ((this.displayNameDrafts[index] ?? '') !== (this.baselineDisplayNames[index] ?? '')) {
                return true;
            }
        }
        return false;
    }

    hasUnappliedChanges() {
        return this.pendingApply && (this.draftsChanged() || this.displayNamesChanged());
    }

    markApplied() {
        for (let index = 0; index < getViewTargetCount(); index++) {
            this.baselineDrafts[index] = this.drafts[index] ?? '';
            this.baselineDisplayNames[index] = this.displayNameDrafts[index] ?? getBuiltinShaderDisplayName(index);
        }
        this.pendingApply = false;
        this.configDraftPending = false;
        this.compileErrorActive = false;
        this.errorKind = null;
        this.lastErrorMessage = '';
        this.disarmClearChanges();
        this.panel.updateShaderHeaderActions?.();
    }

    setCompileError(message, { kind = 'shader' } = {}) {
        this.lastErrorMessage = message
            || (kind === 'config' ? 'Failed to save config' : 'Shader compilation failed');
        this.compileErrorActive = true;
        this.errorKind = kind;
        if (kind === 'shader') {
            this.pendingApply = true;
        }
        this.panel.updateShaderHeaderActions?.();
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

    getErrorMessage(mod, fallback) {
        const errorPtr = mod._getLastShaderError?.();
        if (!errorPtr) {
            return fallback;
        }
        const message = mod.UTF8ToString(errorPtr);
        mod._freeShaderString(errorPtr);
        return message || fallback;
    }

    setViewSource(mod, index, draft) {
        if (mod.ccall) {
            return mod.ccall('setViewSource', 'number', ['number', 'string'], [index, draft]);
        }
        return mod._setViewSource(index, draft);
    }

    async persistDrafts() {
        const mod = this.panel.parentApp.module;
        const FS = mod?.FS;
        if (!FS || !mod._setViewSource) {
            return true;
        }

        this.ensurePersistViewsDir(FS);

        for (let index = 0; index < getViewTargetCount(); index++) {
            const draft = this.drafts[index] ?? '';
            const baseline = this.baselineDrafts[index] ?? '';
            const persistPath = `/persist/views/${index}.wgsl`;

            if (draft !== baseline) {
                FS.writeFile(persistPath, draft);
                if (!this.setViewSource(mod, index, draft)) {
                    this.setCompileError(this.getErrorMessage(mod, `Invalid shader for target ${index}`));
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
            this.setCompileError(this.getErrorMessage(mod, 'Shader compilation failed'));
            return false;
        }

        this.markApplied();
        return true;
    }

    mergeCustomViewsIntoConfig(config) {
        if (!config.customViews) {
            config.customViews = {};
        }
        for (let index = 0; index < getViewTargetCount(); index++) {
            const key = String(index);
            const draft = this.drafts[index] ?? '';
            const baseline = this.baselineDrafts[index] ?? '';
            const displayName = this.getDisplayName(index);
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

    disarmClearChanges() {
        this.clearChangesArmed = false;
        if (this.clearChangesTimer) {
            clearTimeout(this.clearChangesTimer);
            this.clearChangesTimer = null;
        }
    }

    handleClearChangesClick() {
        if (!this.clearChangesArmed) {
            this.clearChangesArmed = true;
            this.panel.updateShaderHeaderActions?.();
            this.clearChangesTimer = setTimeout(() => {
                this.clearChangesArmed = false;
                this.clearChangesTimer = null;
                this.panel.updateShaderHeaderActions?.();
            }, 3000);
            return;
        }

        this.disarmClearChanges();
        this.panel.updateShaderHeaderActions?.();
        void this.clearAllChanges();
    }

    async clearAllChanges() {
        if (this.editorEditing) {
            this.cancelEditorEdit();
        }

        const mod = this.panel.parentApp.module;
        const FS = mod?.FS;
        for (let index = 0; index < getViewTargetCount(); index++) {
            if (mod?._resetViewSource) {
                mod._resetViewSource(index);
            }
            if (FS) {
                const persistPath = `/persist/views/${index}.wgsl`;
                try {
                    if (FS.analyzePath(persistPath).exists) {
                        FS.unlink(persistPath);
                    }
                } catch (_) { /* ignore */ }
            }
            this.displayNameDrafts[index] = getBuiltinShaderDisplayName(index);
            this.baselineDisplayNames[index] = this.displayNameDrafts[index];
        }

        if (mod?._applyViewShaders && mod._applyViewShaders() !== 0) {
            this.setCompileError(this.getErrorMessage(mod, 'Shader compilation failed'));
            return false;
        }

        this.initializeDrafts();
        for (let index = 0; index < getViewTargetCount(); index++) {
            this.displayNameDrafts[index] = getBuiltinShaderDisplayName(index);
            this.baselineDisplayNames[index] = this.displayNameDrafts[index];
        }
        this.pendingApply = false;
        this.configDraftPending = false;
        this.compileErrorActive = false;
        this.errorKind = null;
        this.lastErrorMessage = '';
        this.loadEditorForTarget(this.targetIndex);

        const runtime = readCommittedConfig();
        if (runtime) {
            const next = structuredClone(runtime);
            delete next.customViews;
            replaceCommittedConfig(next);
            writeConfigToFs(next, { persist: true });
        }
        const baseline = getSettingsBaseline();
        if (baseline) {
            delete baseline.customViews;
        }
        this.panel.parentApp.updatePlotLabels?.();
        this.panel.updateShaderHeaderActions?.();
        return true;
    }

    handleKeyDown(e) {
        const inRenameInput = e.target instanceof HTMLInputElement &&
            e.target.classList.contains('shader-explorer-rename-input');
        const inShaderEditor = e.target instanceof HTMLTextAreaElement &&
            (e.target.classList.contains('shader-helpers-editor') ||
                e.target.classList.contains('shader-draw-body') ||
                e.target.classList.contains('shader-config-editor'));

        if (inRenameInput) {
            return true;
        }

        if (inShaderEditor && this.editorEditing) {
            if (e.key === 'Escape') {
                e.preventDefault();
                e.stopPropagation();
                this.cancelEditorEdit();
            }
            return true;
        }

        if (this.renamingIndex !== null && e.key === 'Escape') {
            e.preventDefault();
            e.stopPropagation();
            this.cancelRename();
            return true;
        }

        return false;
    }
}
