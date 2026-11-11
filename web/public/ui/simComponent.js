// Sim viewport shell is defined in index.html; this module holds sim-specific render helpers if needed.

export function getSimCanvas() {
    return document.querySelector('#canvas');
}

export function getLoadingOverlay() {
    return document.getElementById('loadingOverlay');
}
