import { SPEC_TAB, SPEC_URL, SPEC_IFRAME_NAV_OFFSET_PX } from './tabSpecComponent.js';

export { SPEC_TAB, SPEC_URL };

export class SpecPane {
    constructor() {
        this.root = null;
        this.frame = null;
        this.frameLoaded = false;
    }

    mount(parent) {
        this.root = document.createElement('div');
        this.root.className = 'settings-tab-pane hidden spec-pane-root';
        this.root.dataset.tab = SPEC_TAB;

        const pane = document.createElement('div');
        pane.className = 'spec-pane';

        const frameWrap = document.createElement('div');
        frameWrap.className = 'spec-pane-frame-wrap';
        frameWrap.style.setProperty('--spec-iframe-nav-offset', `${SPEC_IFRAME_NAV_OFFSET_PX}px`);

        this.frame = document.createElement('iframe');
        this.frame.className = 'spec-pane-frame';
        this.frame.title = 'Katara spec';
        // Panel teardown destroys the iframe DOM; SpecPane is reused across opens.
        this.frameLoaded = false;
        frameWrap.appendChild(this.frame);

        pane.appendChild(frameWrap);
        this.root.appendChild(pane);
        parent.appendChild(this.root);
        return this.root;
    }

    ensureFrameLoaded() {
        if (!this.frame || this.frameLoaded) return;
        this.frame.src = SPEC_URL;
        this.frameLoaded = true;
    }

    setVisible(visible) {
        this.root?.classList.toggle('hidden', !visible);
        if (visible) {
            this.ensureFrameLoaded();
        }
    }
}
