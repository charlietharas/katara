import { html, render } from './component.js';

export const SETTINGS_TAB = 'settings';

export function renderSettingsLayout(sections) {
    const leftSections = sections.filter((section) => section.column !== 'right');
    const rightSections = sections.filter((section) => section.column === 'right');
    const rightTopSections = rightSections.filter((section) => section.placement !== 'bottom');
    const rightBottomSections = rightSections.filter((section) => section.placement === 'bottom');

    return html`
        <div class="settings-columns">
            <div class="settings-col settings-col-left">
                ${leftSections.map((section) => section.renderView())}
            </div>
            <div class="settings-col settings-col-right">
                <div class="settings-col-right-top">
                    ${rightTopSections.map((section) => section.renderView())}
                </div>
                <div class="settings-col-right-bottom">
                    ${rightBottomSections.map((section) => section.renderView())}
                </div>
            </div>
        </div>
    `;
}

export function mountSettingsLayout(root, sections) {
    if (!root) return;
    render(renderSettingsLayout(sections), root);
}
