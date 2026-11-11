/** @type {object | null} */
let runtimeApp = null;

/** @type {Record<string, unknown> | null} */
let committedConfig = null;

/** @type {Record<string, unknown> | null} */
let settingsBaselineConfig = null;

/** @type {Record<string, unknown> | null} */
let settingsDraftConfig = null;

function cloneConfig(config) {
    return config == null ? null : structuredClone(config);
}

function patchPath(root, path, value) {
    const result = structuredClone(root ?? {});
    const keys = path.split('.');
    let current = result;
    for (let i = 0; i < keys.length - 1; i++) {
        const key = keys[i];
        if (!(key in current) || typeof current[key] !== 'object' || current[key] === null) {
            current[key] = {};
        }
        current = current[key];
    }
    current[keys[keys.length - 1]] = value;
    return result;
}

function getFs() {
    return runtimeApp?.module?.FS ?? null;
}

export function setRuntimeApp(app) {
    runtimeApp = app;
}

export function getRuntimeApp() {
    return runtimeApp;
}

export function getCommittedConfig() {
    return committedConfig;
}

export function loadCommittedConfig() {
    const FS = getFs();
    if (!FS) return committedConfig;
    try {
        const configText = FS.readFile('/config.json', { encoding: 'utf8' });
        committedConfig = cloneConfig(JSON.parse(configText));
        return committedConfig;
    } catch (err) {
        console.warn('Could not read /config.json:', err);
        return committedConfig;
    }
}

export function replaceCommittedConfig(config) {
    committedConfig = cloneConfig(config);
    return committedConfig;
}

export function patchCommittedConfigPath(path, value) {
    committedConfig = patchPath(committedConfig, path, value);
    return committedConfig;
}

/** Cache-first read with FS fallback via load. */
export function readCommittedConfig() {
    return committedConfig ?? loadCommittedConfig() ?? null;
}

export function beginSettingsSession(sourceConfig = null) {
    const source = cloneConfig(sourceConfig) ?? cloneConfig(committedConfig) ?? loadCommittedConfig();
    settingsBaselineConfig = cloneConfig(source);
    settingsDraftConfig = cloneConfig(source);
    return settingsDraftConfig;
}

export function getSettingsBaseline() {
    return settingsBaselineConfig;
}

export function getSettingsDraft() {
    return settingsDraftConfig;
}

export function replaceSettingsDraft(config) {
    settingsDraftConfig = cloneConfig(config);
    return settingsDraftConfig;
}

export function patchSettingsDraftPath(path, value) {
    settingsDraftConfig = patchPath(settingsDraftConfig, path, value);
    return settingsDraftConfig;
}

export function resetSettingsDraft(defaults) {
    settingsDraftConfig = cloneConfig(defaults);
    return settingsDraftConfig;
}

export function commitSettingsDraft(draft = settingsDraftConfig) {
    if (!draft) return committedConfig;
    committedConfig = cloneConfig(draft);
    clearSettingsSession();
    return committedConfig;
}

export function cancelSettingsSession() {
    if (settingsBaselineConfig) {
        committedConfig = cloneConfig(settingsBaselineConfig);
    }
    clearSettingsSession();
    return committedConfig;
}

export function clearSettingsSession() {
    settingsBaselineConfig = null;
    settingsDraftConfig = null;
}

export function hasSettingsSession() {
    return settingsDraftConfig != null || settingsBaselineConfig != null;
}

export function writeConfigToFs(configOrText, { persist = false } = {}) {
    const FS = getFs();
    if (!FS) return;
    const text = typeof configOrText === 'string'
        ? configOrText
        : JSON.stringify(configOrText, null, 4);
    FS.writeFile('/config.json', text);
    if (persist && runtimeApp?.idbfsAvailable) {
        FS.writeFile('/persist/config.json', text);
    }
}

export async function persistConfigToIdb() {
    if (!runtimeApp?.idbfsAvailable) return;
    const FS = getFs();
    if (!FS) return;
    await new Promise((resolve) => {
        FS.syncfs(false, (err) => {
            if (err) console.error('syncfs failed:', err);
            else console.log('syncfs completed');
            resolve();
        });
    });
    localStorage.setItem('katara_needs_restore', 'true');
}
