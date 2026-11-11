import { KataraWebApp } from './ui/simEffect.js';
import { setRuntimeApp } from './config.js';

document.addEventListener('DOMContentLoaded', () => {
    const app = new KataraWebApp();
    setRuntimeApp(app);
    app.init();
});
