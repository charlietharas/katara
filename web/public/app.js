import { MediaPipeHandTracker } from './mediapipe.js';
import {
    SettingsPanel,
    createSkeletonSections,
    INPUT_MODE_INT,
    isMouseInputMode,
    measureElementWidth,
    measureElementHeight,
    getSimViewportSize,
    HAND_CONNECTIONS,
    ACTIVE_LANDMARKS_BY_MODE,
    HAND_COLORS,
} from './settings.js';

const VIEWPORT_NAMES = ['viewport_1', 'viewport_2', 'viewport_3', 'viewport_4'];

function isValidLayoutRect(rect) {
    return rect
        && Number.isFinite(rect.x)
        && Number.isFinite(rect.y)
        && Number.isFinite(rect.width)
        && Number.isFinite(rect.height);
}

function fsSyncfs(FS, populate) {
    return new Promise((resolve) => {
        FS.syncfs(populate, (err) => {
            if (err) console.error('syncfs failed:', err);
            else console.log('syncfs completed');
            resolve();
        });
    });
}

function getHandTrackingInfo(hand, modes) {
    const isLeftHand = hand.handedness === 'Right';
    const mode = isLeftHand ? modes.left : modes.right;
    return {
        isLeftHand,
        mode,
        color: isLeftHand ? HAND_COLORS.left : HAND_COLORS.right,
        activeSet: new Set(ACTIVE_LANDMARKS_BY_MODE[mode]),
    };
}

class KataraWebApp {

    async waitForNav() {
        if (document.querySelector('nav h2')) return;

        const nav = document.querySelector('nav');
        if (!nav) return;

        await new Promise(resolve => {
            const observer = new MutationObserver(() => {
                if (document.querySelector('nav h2')) {
                    observer.disconnect();
                    resolve();
                }
            });
            observer.observe(nav, { childList: true, subtree: true });
            setTimeout(() => {
                observer.disconnect();
                resolve();
            }, 5000);
        });
    }

    refreshSimLayout() {
        const simCanvas = document.querySelector('#canvas');
        const size = getSimViewportSize();
        simCanvas.width = size.width;
        simCanvas.height = size.height;
        this.syncLayoutStateFromConfig();
        this.applyLayoutUI(this.initLayoutFromCpp());
    }

    applyLayoutUI(layout = this.layoutPixels) {
        this.positionCameraPanel(layout);
        this.updateCameraUi();
        this.updateSimControlsVisibility();
        this.positionControls(layout);
        this.updateViewportButtons();
        this.updatePlotLabels();
    }

    readConfig() {
        if (!this.module?.FS) return null;
        try {
            const configText = this.module.FS.readFile('/config.json', { encoding: 'utf8' });
            return JSON.parse(configText);
        } catch (err) {
            console.warn('Could not read /config.json:', err);
            return null;
        }
    }

    clearHandTracking() {
        if (!this.module) return;
        this.module._updateFingertips(0, 0);
        this.module._updateLineSegments(0, 0);
    }

    async init() {
        this.isInkMode = false;
        this.labelsEnabled = true;
        this.buttonsEnabled = true;
        this.camerasEnabled = true;
        this.inkAspectRatio = 1.0;
        this.cameraAspectRatio = 4.0 / 3.0;
        this.viewportControllers = new Map();
        this.plotLabelElements = new Map();

        // Camera availability state tracking
        this.cameraDetected = false;

        // Show loading overlay
        const loadingOverlay = document.getElementById('loadingOverlay');
        if (loadingOverlay) {
            loadingOverlay.classList.remove('hidden');
        }

        await this.waitForNav();

        // Canvas size will be updated after C++ resizes window
        const simCanvas = document.querySelector('#canvas');
        const initialSize = getSimViewportSize();
        simCanvas.width = initialSize.width;
        simCanvas.height = initialSize.height;

        // Check if we need to restore persisted data (images, config)
        const needsRestore = localStorage.getItem('katara_needs_restore') === 'true';

        const module = await createKataraModule({
            canvas: simCanvas,
            noInitialRun: true // prevent main() from running automatically
        });

        this.module = module;
        const FS = module.FS;

        // Create /persist directory and mount IDBFS
        FS.mkdir('/persist');
        try {
            FS.mount(module.IDBFS, {}, '/persist');
            this.idbfsAvailable = true;
            console.log('IDBFS mounted at /persist');
        } catch (e) {
            console.error('IDBFS mount failed:', e);
            this.idbfsAvailable = false;
        }

        // Sync from IndexedDB and restore files if needed
        if (this.idbfsAvailable && needsRestore) {
            console.log('Syncing from IndexedDB...');
            await fsSyncfs(FS, true);

            // Check what's in /persist
            const contents = FS.readdir('/persist');

            // Restore files to /
            if (contents.includes('config.json')) {
                const persistedConfig = FS.readFile('/persist/config.json', { encoding: 'utf8' });
                FS.writeFile('/config.json', persistedConfig);
            }
            if (contents.includes('uploaded.png')) {
                const img = FS.readFile('/persist/uploaded.png');
                FS.writeFile('/uploaded.png', img);
            }

            // Clear the flag
            localStorage.setItem('katara_needs_restore', 'false');
        }

        // C++ will call kataraOnReady when initialization is complete
        this.configured = false;

        // Wait for C++ to signal readiness (window created, image loaded)
        await new Promise(resolve => {
            window.kataraOnReady = resolve;
            module.callMain([]);
        });

        // Clean up the callback
        window.kataraOnReady = null;

        this.syncLayoutStateFromConfig();
        await this.updateInkAspectRatioFromConfig();

        // Now canvas size matches the (possibly resized) window
        const settledSize = getSimViewportSize();
        simCanvas.width = settledSize.width;
        simCanvas.height = settledSize.height;
        console.log('After C++ init: canvas size', simCanvas.width, 'x', simCanvas.height);
        console.log('Window AR:', (settledSize.width / settledSize.height).toFixed(3));
        const initialLayout = this.initLayoutFromCpp();
        this.applyLayoutUI(initialLayout);
        this.configured = true;

        // 2D context for camera + keypoints
        const camPanel = document.querySelector('.camera-panel');
        if (!camPanel) {
            throw new Error('Camera panel not found');
        }

        // Create camera canvas dynamically
        this.cameraCanvas = document.createElement('canvas');
        this.cameraCanvas.id = 'cameraCanvas';
        // Set initial dimensions, will be adjusted based on camera stream
        this.cameraCanvas.width = 640;
        this.cameraCanvas.height = 480;
        this.cameraCanvas.style.width = '100%';
        this.cameraCanvas.style.display = 'block';
        camPanel.appendChild(this.cameraCanvas);

        console.log('Camera canvas created');

        // Create countdown overlay element
        this.countdownElement = document.createElement('div');
        this.countdownElement.className = 'countdown-overlay';
        document.body.appendChild(this.countdownElement);

        // Get 2D context
        this.cameraCtx = this.cameraCanvas.getContext('2d');
        if (!this.cameraCtx) {
            throw new Error('Could not get 2D context for camera canvas');
        }
        console.log('2D context created successfully');

        await this.setupCamera();
        this.applyInputModeFromConfig();

        // Recompute after camera metadata is ready so camera frame can use stream AR.
        const cameraAlignedLayout = this.initLayoutFromCpp();
        this.applyLayoutUI(cameraAlignedLayout);

        this.setupInkUpload();
        this.setupCameraCapture();
        this.setupResetButton();
        this.setupPauseButton();
        this.setupSettingsButton();
        this.setupKeyboardShortcuts();
        this.setupViewportButtons();
        this.setupPlotLabels();
        this.applyLayoutUI();

        // mediapipe
        this.handTracker = new MediaPipeHandTracker();
        await this.handTracker.init(this.videoElement);

        // Handle window resize (C++ might resize window based on image)
        window.addEventListener('resize', () => {
            if (this.configured) {
                this.refreshSimLayout();
            }
        });

        const viewport = document.querySelector('.sim-viewport');
        if (viewport) {
            this.viewportObserver = new ResizeObserver(() => {
                if (this.configured) {
                    this.refreshSimLayout();
                }
            });
            this.viewportObserver.observe(viewport);
        }

        // Hide loading overlay - everything is already configured
        if (loadingOverlay) {
            loadingOverlay.classList.add('hidden');
        }
        const ar = window.innerWidth / window.innerHeight;
        console.log('Layout settled - AR:', ar.toFixed(3), ar < 1.0 ? '(portrait)' : '(landscape)');

        // start hand tracking loop
        console.log("Hand tracking starting. Say hi!");
        this.processLoop();
    }

    canUseCameraInput() {
        const video = this.videoElement;
        return this.cameraDetected
            && video instanceof HTMLVideoElement
            && video.readyState >= 2
            && video.videoWidth > 0;
    }

    async setupCamera() {
        this.cameraDetected = false;
        this.videoElement = null;

        if (!navigator.mediaDevices?.getUserMedia) {
            console.warn('Camera API unavailable');
            this.renderNoCameraMessage();
            return;
        }

        try {
            const stream = await navigator.mediaDevices.getUserMedia({
                video: { facingMode: 'user' }
            });

            this.videoElement = document.createElement('video');
            this.videoElement.muted = true;
            this.videoElement.playsInline = true;
            this.videoElement.setAttribute('playsinline', '');
            this.videoElement.srcObject = stream;
            document.body.appendChild(this.videoElement);
            this.videoElement.play();
            await new Promise(resolve => this.videoElement.onloadeddata = resolve);

            const videoWidth = this.videoElement.videoWidth;
            const videoHeight = this.videoElement.videoHeight;
            console.log('Camera stream dimensions:', videoWidth, videoHeight);

            if (videoWidth && videoHeight) {
                this.cameraCanvas.width = videoWidth;
                this.cameraCanvas.height = videoHeight;
                this.cameraAspectRatio = videoWidth / videoHeight;
            }

            this.cameraDetected = true;
            console.log('Camera access granted');
        } catch (error) {
            console.warn('Camera access failed:', error.message);
            this.cameraDetected = false;
            this.videoElement = null;
            this.renderNoCameraMessage();
        }
    }

    renderNoCameraMessage() {
        const ctx = this.cameraCtx;
        if (!ctx) return;

        const canvas = this.cameraCanvas;
        const width = canvas.width;
        const height = canvas.height;

        ctx.fillStyle = '#1a1a1a';
        ctx.fillRect(0, 0, width, height);

        ctx.fillStyle = '#fff';
        ctx.font = 'bold 24px monospace';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText('ERR_NO_VIDEO', width / 2, height / 2);
    }

    initLayoutFromCpp() {
        const { width: canvasW, height: canvasH } = getSimViewportSize();
        this.module._initLayout(canvasW, canvasH, this.inkAspectRatio, this.cameraAspectRatio);
        const jsonStr = this.module.FS.readFile('/layout_pixels.json', { encoding: 'utf8' });
        this.layoutPixels = JSON.parse(jsonStr);
        console.log('Layout from C++:', canvasW, 'x', canvasH, this.layoutPixels);
        return this.layoutPixels;
    }

    syncLayoutStateFromConfig() {
        const config = this.readConfig();
        if (!config) return;
        this.isInkMode = config?.rendering?.target === 3;
        this.labelsEnabled = config?.layout?.labelsEnabled !== false;
        this.buttonsEnabled = config?.layout?.buttonsEnabled !== false;
        this.camerasEnabled = config?.layout?.camerasEnabled !== false;
    }

    async updateInkAspectRatioFromConfig() {
        const config = this.readConfig();
        if (!config) return;

        const imagePath = config?.ink?.imagePath;
        if (!imagePath) return;

        try {
            const exists = this.module.FS.analyzePath(imagePath).exists;
            if (!exists) return;
            const imageBytes = this.module.FS.readFile(imagePath);
            const imageBlob = new Blob([imageBytes], { type: 'image/png' });
            const imageAspectRatio = await this.getImageAspectRatio(imageBlob);
            if (imageAspectRatio > 0) {
                this.inkAspectRatio = imageAspectRatio;
            }
        } catch (err) {
            console.warn('Could not read ink image for AR:', err);
        }
    }

    async getImageAspectRatio(imageSource) {
        try {
            const bitmap = await createImageBitmap(imageSource);
            const imageAspectRatio = bitmap.width > 0 && bitmap.height > 0
                ? bitmap.width / bitmap.height
                : 0;
            bitmap.close();
            return imageAspectRatio;
        } catch (err) {
            console.warn('Could not decode image for AR:', err);
            return 0;
        }
    }

    positionCameraPanel(layout) {
        const camPanel = document.querySelector('.camera-panel');
        if (!camPanel) {
            return;
        }

        if (!this.camerasEnabled) {
            camPanel.style.display = 'none';
            return;
        }

        const camFrame = layout?.camera_frame;
        if (camFrame && camFrame.width > 0 && camFrame.height > 0 && this.cameraCanvas) {
            camPanel.style.display = '';
            camPanel.style.position = 'absolute';
            camPanel.style.left = camFrame.x + 'px';
            camPanel.style.top = camFrame.y + 'px';
            camPanel.style.width = camFrame.width + 'px';
            camPanel.style.height = camFrame.height + 'px';

            this.cameraCanvas.style.width = '100%';
            this.cameraCanvas.style.height = '100%';
            this.cameraCanvas.style.maxWidth = 'none';
        } else {
            camPanel.style.display = 'none';
        }
    }

    canCaptureFromCamera() {
        return this.camerasEnabled && this.canUseCameraInput();
    }

    updateCameraUi() {
        const camPanel = document.querySelector('.camera-panel');
        if (camPanel && !this.camerasEnabled) {
            camPanel.style.display = 'none';
        }

        const cheeseBtn = document.getElementById('cheeseBtn');
        if (cheeseBtn) {
            const disabled = !this.canCaptureFromCamera();
            cheeseBtn.disabled = disabled;
            cheeseBtn.classList.toggle('disabled', disabled);
            if (disabled && !this.cameraDetected) {
                cheeseBtn.title = 'No camera detected';
            } else {
                cheeseBtn.title = 'Take picture from webcam (C)';
            }
        }
    }

    getGridBounds(layout) {
        if (!layout) return null;

        const viewportRects = VIEWPORT_NAMES.slice(0, 3)
            .map(name => layout[name])
            .filter(isValidLayoutRect);

        const candidateRects = viewportRects.length > 0
            ? viewportRects
            : Object.values(layout).filter(isValidLayoutRect);

        if (candidateRects.length === 0) return null;

        const minX = Math.min(...candidateRects.map(rect => rect.x));
        const minY = Math.min(...candidateRects.map(rect => rect.y));
        const maxX = Math.max(...candidateRects.map(rect => rect.x + rect.width));
        const maxY = Math.max(...candidateRects.map(rect => rect.y + rect.height));

        return {
            x: minX,
            y: minY,
            width: Math.max(0, maxX - minX),
            height: Math.max(0, maxY - minY)
        };
    }

    positionControls(layout) {
        const controls = document.querySelector('.sim-controls');
        const bounds = this.getGridBounds(layout);
        if (!controls || !bounds) return;

        const margin = 16;
        const controlsWidth = measureElementWidth(controls);
        const controlsHeight = measureElementHeight(controls);
        const targetLeft = bounds.x + bounds.width - controlsWidth - margin;
        const targetTop = bounds.y + bounds.height - controlsHeight - margin;

        controls.style.position = 'absolute';
        controls.style.left = `${Math.max(bounds.x, targetLeft)}px`;
        controls.style.top = `${Math.max(bounds.y, targetTop)}px`;
        controls.style.right = 'auto';
        controls.style.bottom = 'auto';
    }

    setupInkUpload() {
        const uploadBtn = document.getElementById('uploadInkBtn');
        const fileInput = document.getElementById('inkImageInput');

        if (!uploadBtn || !fileInput) {
            console.error('Upload button or file input not found');
            return;
        }

        uploadBtn.addEventListener('click', () => fileInput.click());

        fileInput.addEventListener('change', async (e) => {
            const file = e.target.files[0];
            if (!file) return;

            if (!file.name.toLowerCase().endsWith('.png')) {
                alert('Please select a PNG image file.');
                return;
            }

            await this.handleImageUpload(file);
            fileInput.value = ''; // Reset for next upload
        });
    }

    async handleImageUpload(file) {
        const FS = this.module.FS;

        try {
            const uploadedAspectRatio = await this.getImageAspectRatio(file);
            if (uploadedAspectRatio > 0) {
                this.inkAspectRatio = uploadedAspectRatio;
            }

            // Read file as ArrayBuffer
            const arrayBuffer = await file.arrayBuffer();
            const uint8Array = new Uint8Array(arrayBuffer);
            console.log('Image loaded: ' + uint8Array.length + ' bytes');

            // Write to /persist and root
            FS.writeFile('/persist/uploaded.png', uint8Array);
            FS.writeFile('/uploaded.png', uint8Array);

            // Update config to use uploaded image and enable ink mode
            const configText = FS.readFile('/config.json', { encoding: 'utf8' });
            const config = JSON.parse(configText);
            config.ink.imagePath = '/uploaded.png';
            config.rendering.target = 3;
            const newConfigText = JSON.stringify(config, null, 4);

            // Write config to /persist and root
            FS.writeFile('/persist/config.json', newConfigText);
            FS.writeFile('/config.json', newConfigText);

            console.log('Files written, syncing to IndexedDB...');

            // Sync to IndexedDB
            await fsSyncfs(FS, false);

            localStorage.setItem('katara_needs_restore', 'true');

            console.log('Reloading config...');
            this.module._reloadConfig(1 | 4);
            this.refreshSimLayout();
            if (this.settingsPanel?.isOpen) {
                await this.settingsPanel.refreshEnvironmentControls();
            }

        } catch (err) {
            console.error('Failed to upload image:', err);
            alert('Failed to upload image: ' + err.message);
        }
    }

    setupCameraCapture() {
        const cheeseBtn = document.getElementById('cheeseBtn');
        if (!cheeseBtn) {
            console.error('Cheese button not found');
            return;
        }

        cheeseBtn.addEventListener('click', () => {
            if (!this.canCaptureFromCamera()) {
                return;
            }
            this.captureFromCamera();
        });
    }

    resetSimulation() {
        if (this.module && this.module._resetFluidField) {
            this.module._resetFluidField();
            this.syncViewportTargetsToCpp();
        } else {
            console.error('resetFluidField not available');
        }
    }

    mergeViewportTargetsIntoConfig(config) {
        if (!config.layout) {
            config.layout = {};
        }
        if (!config.layout.components) {
            config.layout.components = {};
        }
        this.viewportControllers.forEach((controller, name) => {
            if (!config.layout.components[name]) {
                config.layout.components[name] = {};
            }
            config.layout.components[name].target = controller.target;
            config.layout.components[name].velocity = controller.velocity;
        });
    }

    syncViewportTargetsToCpp() {
        if (!this.module?._setViewportTarget || !this.module?._setViewportVelocity) {
            return;
        }
        this.viewportControllers.forEach((controller) => {
            this.module._setViewportTarget(controller.viewportIndex, controller.target);
            this.module._setViewportVelocity(
                controller.viewportIndex,
                controller.velocity ? 1 : 0
            );
        });
    }

    togglePause() {
        this.simulationPaused = !this.simulationPaused;

        const pauseBtn = document.getElementById('pauseBtn');
        const pauseBtnIcon = document.getElementById('pauseBtnIcon');
        const pauseBtnLabel = document.getElementById('pauseBtnLabel');
        if (!pauseBtn || !pauseBtnIcon || !pauseBtnLabel) {
            console.error('Pause button not found');
            return;
        }

        if (this.module && this.module._setSimulationPaused) {
            this.module._setSimulationPaused(this.simulationPaused ? 1 : 0);
        } else {
            console.error('setSimulationPaused not available');
        }

        if (this.simulationPaused) {
            this.clearHandTracking();
            pauseBtnIcon.innerHTML = '<polygon points="8,5 19,12 8,19"></polygon>';
            pauseBtnIcon.setAttribute('fill', 'currentColor');
            pauseBtnIcon.setAttribute('stroke', 'currentColor');
            pauseBtnIcon.setAttribute('stroke-width', '2');
            pauseBtnIcon.setAttribute('stroke-linejoin', 'round');
            pauseBtnLabel.textContent = '[P]';
            pauseBtn.title = 'Resume simulation (P)';
        } else {
            pauseBtnIcon.innerHTML = '<rect x="6" y="4" width="4" height="16" rx="1"></rect><rect x="14" y="4" width="4" height="16" rx="1"></rect>';
            pauseBtnIcon.removeAttribute('stroke');
            pauseBtnIcon.removeAttribute('stroke-width');
            pauseBtnIcon.removeAttribute('stroke-linejoin');
            pauseBtnLabel.textContent = '[P]';
            pauseBtn.title = 'Pause simulation (P)';
        }
    }

    setupKeyboardShortcuts() {
        document.addEventListener('keydown', (event) => {
            const target = event.target;
            if (target instanceof HTMLInputElement ||
                target instanceof HTMLTextAreaElement ||
                target instanceof HTMLSelectElement ||
                target.isContentEditable) {
                return;
            }

            if (event.ctrlKey || event.metaKey || event.altKey) {
                return;
            }

            switch (event.key.toLowerCase()) {
                case 'c':
                    if (!this.canCaptureFromCamera()) {
                        break;
                    }
                    event.preventDefault();
                    this.captureFromCamera();
                    break;
                case 'p':
                    event.preventDefault();
                    this.togglePause();
                    break;
                case 'r':
                    event.preventDefault();
                    this.resetSimulation();
                    break;
                case 'u': {
                    event.preventDefault();
                    const uploadBtn = document.getElementById('uploadInkBtn');
                    if (uploadBtn instanceof HTMLButtonElement) {
                        uploadBtn.click();
                    }
                    break;
                }
                case 's':
                    event.preventDefault();
                    const settingsBtn = document.getElementById('settingsBtn');
                    if (settingsBtn) settingsBtn.click();
                    break;
                case '1':
                case '2':
                case '3':
                case '4':
                    event.preventDefault();
                    this.cycleViewportTarget(Number(event.key));
                    break;
            }
        });
    }

    setupResetButton() {
        const resetBtn = document.getElementById('resetBtn');
        if (!resetBtn) {
            console.error('Reset button not found');
            return;
        }

        resetBtn.addEventListener('click', () => this.resetSimulation());
    }

    setupPauseButton() {
        this.simulationPaused = false;
        const pauseBtn = document.getElementById('pauseBtn');
        const pauseBtnIcon = document.getElementById('pauseBtnIcon');
        const pauseBtnLabel = document.getElementById('pauseBtnLabel');
        if (!pauseBtn || !pauseBtnIcon || !pauseBtnLabel) {
            console.error('Pause button not found');
            return;
        }

        pauseBtn.addEventListener('click', () => this.togglePause());
    }

    setupSettingsButton() {
        const settingsBtn = document.getElementById('settingsBtn');
        if (!settingsBtn) {
            console.error('Settings button not found');
            return;
        }

        settingsBtn.addEventListener('click', () => {
            if (!this.settingsPanel) {
                this.settingsPanel = new SettingsPanel(this);

                let currentConfig = this.readConfig() ?? {};
                window.kataraConfig = currentConfig;

                const sections = createSkeletonSections(currentConfig, {
                    cameraDetected: this.canUseCameraInput(),
                });
                sections.forEach(section => this.settingsPanel.addSection(section));
            }

            this.settingsPanel.open();
        });
    }

    setupViewportButtons() {
        this.syncViewportButtons();
    }

    setupPlotLabels() {
        this.syncPlotLabels();
    }

    getActiveViewportNames() {
        return VIEWPORT_NAMES.filter((name) => {
            const rect = this.layoutPixels?.[name];
            return isValidLayoutRect(rect) && rect.width > 0 && rect.height > 0;
        });
    }

    cycleViewportTarget(viewportNumber) {
        const controller = this.viewportControllers.get(`viewport_${viewportNumber}`);
        if (controller) {
            controller.cycleTarget();
        }
    }

    syncViewportButtons() {
        const activeNames = new Set(this.getActiveViewportNames());

        // Remove stale controllers
        this.viewportControllers.forEach((controller, name) => {
            if (!activeNames.has(name)) {
                controller.destroy();
                this.viewportControllers.delete(name);
            }
        });

        // Create missing controllers
        activeNames.forEach((name) => {
            if (this.viewportControllers.has(name)) {
                return;
            }
            const layoutRect = this.layoutPixels[name];
            const target = layoutRect.target ?? 2;
            const velocity = !!layoutRect.velocity;
            const index = Number.parseInt(name.split('_')[1], 10) - 1;
            const controller = new ViewportController(index, name, target, velocity, layoutRect, this);
            controller.createTargetButton();
            this.viewportControllers.set(name, controller);
        });
    }

    updateSimControlsVisibility() {
        const controls = document.querySelector('.sim-controls');
        if (controls) {
            controls.style.display = this.buttonsEnabled ? '' : 'none';
        }
    }

    updateViewportButtons() {
        this.syncViewportButtons();
        this.viewportControllers.forEach((controller, name) => {
            controller.setButtonsVisible(this.buttonsEnabled);
            const layoutRect = this.layoutPixels[name];
            if (layoutRect && layoutRect.width > 0 && layoutRect.height > 0) {
                if (layoutRect.target !== undefined) {
                    controller.target = layoutRect.target;
                    controller.updateDisplay();
                }
                if (layoutRect.velocity !== undefined) {
                    controller.velocity = !!layoutRect.velocity;
                    controller.updateVelocityDisplay();
                }
                controller.updateLayout(layoutRect);
            }
        });
        this.updateSimControlsVisibility();
        if (this.buttonsEnabled) {
            requestAnimationFrame(() => {
                this.viewportControllers.forEach((controller) => controller.positionButtons());
                this.positionControls(this.layoutPixels);
            });
        }
    }

    getActiveLabelComponents() {
        const labels = [
            ['camera_frame', 'CAM'],
            ['viewport_1', 'PANE_1'],
            ['viewport_2', 'PANE_2'],
            ['viewport_3', 'PANE_3'],
            ['viewport_4', 'PANE_4'],
            ['density_histogram', 'DENSITY'],
            ['velocity_histogram', 'VELOCITY'],
            ['entropy_time_series', 'ENTROPY'],
            ['volume_time_series', 'VOLUME']
        ];

        return labels.filter(([name]) => {
            const rect = this.layoutPixels?.[name];
            const enabled = name.includes('histogram') || name.includes('time_series')
                ? rect?.enabled !== false
                : true;
            return enabled && rect && rect.width > 0 && rect.height > 0;
        });
    }

    syncPlotLabels() {
        const activeEntries = this.getActiveLabelComponents();
        const activeNames = new Set(activeEntries.map(([name]) => name));
        const viewport = document.querySelector('.sim-viewport');
        if (!viewport) return;

        this.plotLabelElements.forEach((element, name) => {
            if (!activeNames.has(name)) {
                if (element.parentNode) element.parentNode.removeChild(element);
                this.plotLabelElements.delete(name);
            }
        });

        activeEntries.forEach(([name, displayLabel]) => {
            if (this.plotLabelElements.has(name)) {
                return;
            }
            const label = document.createElement('div');
            label.className = 'plot-label';
            label.textContent = displayLabel;
            viewport.appendChild(label);
            this.plotLabelElements.set(name, label);
        });
    }

    updatePlotLabels() {
        this.syncPlotLabels();
        if (!this.labelsEnabled) {
            this.plotLabelElements.forEach((label) => {
                label.style.display = 'none';
            });
            this.viewportControllers.forEach((controller) => controller.positionButtons());
            return;
        }
        this.plotLabelElements.forEach((label, name) => {
            const rect = this.layoutPixels?.[name];
            if (!rect || rect.width <= 0 || rect.height <= 0) {
                label.style.display = 'none';
                return;
            }

            label.style.display = '';
            label.style.position = 'absolute';
            label.style.left = `${rect.x + 8}px`;
            label.style.top = `${rect.y + 8}px`;
            label.style.maxWidth = `${Math.max(0, rect.width - 16)}px`;
        });
        this.viewportControllers.forEach((controller) => controller.positionButtons());
    }

    async captureFromCamera() {
        if (!this.canCaptureFromCamera()) {
            return;
        }

        // Countdown sequence
        for (let i = 3; i > 0; i--) {
            this.countdownElement.textContent = i;
            await new Promise(resolve => setTimeout(resolve, 1000));
        }
        this.countdownElement.textContent = '';

        // Capture frame from video element
        const tempCanvas = document.createElement('canvas');
        tempCanvas.width = this.videoElement.videoWidth;
        tempCanvas.height = this.videoElement.videoHeight;
        const tempCtx = tempCanvas.getContext('2d');
        tempCtx.drawImage(this.videoElement, 0, 0);

        // Convert to blob and create File object
        tempCanvas.toBlob(async (blob) => {
            const file = new File([blob], 'camera-capture.png', { type: 'image/png' });
            await this.handleImageUpload(file);
        }, 'image/png');
    }

    getHandModes() {
        const hands = window.kataraConfig?.hands;
        return {
            left: hands?.left || 'full',
            right: hands?.right || 'full',
        };
    }

    drawKeypoints(hands) {
        const ctx = this.cameraCtx;
        const width = this.cameraCanvas.width;
        const height = this.cameraCanvas.height;
        const modes = this.getHandModes();
        const grey = HAND_COLORS.overlayGrey;

        for (const hand of hands) {
            const { mode, color, activeSet } = getHandTrackingInfo(hand, modes);

            ctx.lineWidth = 3;
            for (const [i, j] of HAND_CONNECTIONS) {
                const bothActive = mode !== 'joints' && activeSet.has(i) && activeSet.has(j);
                ctx.strokeStyle = bothActive ? color : grey;
                const p1 = hand.landmarks[i];
                const p2 = hand.landmarks[j];
                ctx.beginPath();
                ctx.moveTo(p1.x * width, p1.y * height);
                ctx.lineTo(p2.x * width, p2.y * height);
                ctx.stroke();
            }

            // keypoints
            for (let i = 0; i < hand.landmarks.length; i++) {
                const lm = hand.landmarks[i];
                const active = activeSet.has(i);
                ctx.fillStyle = active ? color : grey;
                const r = active ? 5 : 3;
                ctx.beginPath();
                ctx.arc(lm.x * width, lm.y * height, r, 0, 2 * Math.PI);
                ctx.fill();
            }
        }
    }

    async processLoop() {
        try {
            if (!this.simulationPaused) {
                if (!this.camerasEnabled) {
                    if (!this.isMouseInput()) this.clearHandTracking();
                } else {
                    const video = this.videoElement;
                    if (!(video instanceof HTMLVideoElement) || video.readyState < 2 || video.videoWidth === 0) {
                        this.renderNoCameraMessage();
                    } else if (this.isMouseInput()) {
                        this.cameraCtx.drawImage(video, 0, 0, this.cameraCanvas.width, this.cameraCanvas.height);
                        this.clearHandTracking();
                    } else {
                        const result = await this.handTracker.detectHands();
                        this.cameraCtx.drawImage(video, 0, 0, this.cameraCanvas.width, this.cameraCanvas.height);

                        if (result.hands?.length > 0) {
                            this.drawKeypoints(result.hands);
                        }

                        const allLandmarks = result.landmarks;
                        if (allLandmarks?.length > 0) {
                            const modes = this.getHandModes();
                            if (result.hands) {
                                const handCount = Math.min(result.hands.length, 2);
                                for (let handIdx = 0; handIdx < handCount; handIdx++) {
                                    const { activeSet } = getHandTrackingInfo(result.hands[handIdx], modes);
                                    const base = handIdx * 21;
                                    for (let i = 0; i < 21; i++) {
                                        if (!activeSet.has(i)) allLandmarks[base + i].present = false;
                                    }
                                }
                            }

                            const ptrAll = this.module._malloc(allLandmarks.length * 16);
                            const heap = this.module.HEAPF32;
                            let offset = ptrAll / 4;
                            for (let i = 0; i < allLandmarks.length; i++) {
                                const lm = allLandmarks[i];
                                heap[offset++] = 1.0 - lm.x;
                                heap[offset++] = lm.y;
                                heap[offset++] = lm.z;
                                heap[offset++] = lm.present ? 1.0 : 0.0;
                            }

                            this.module._updateFingertips(ptrAll, allLandmarks.length);

                            if (result.hands) {
                                const handCount = Math.min(result.hands.length, 2);
                                for (let handIdx = 0; handIdx < handCount; handIdx++) {
                                    const { mode } = getHandTrackingInfo(result.hands[handIdx], modes);
                                    if (mode === 'joints') {
                                        const base = ptrAll / 4 + handIdx * 21 * 4;
                                        for (let i = 0; i < 21; i++) {
                                            heap[base + i * 4 + 3] = 0.0;
                                        }
                                    }
                                }
                            }

                            this.module._updateLineSegments(ptrAll, allLandmarks.length);
                            this.module._free(ptrAll);
                            this.frameCount++;
                        } else {
                            this.clearHandTracking();
                        }
                    }
                }
            }
        } catch (err) {
            console.error('Hand tracking error:', err);
        }

        requestAnimationFrame(() => this.processLoop());
    }

    isMouseInput() {
        return isMouseInputMode(window.kataraConfig?.inputMode);
    }

    applyInputModeFromConfig() {
        const config = this.readConfig();
        if (!config) return;

        window.kataraConfig = config;

        let mode = config.inputMode ?? 'hand';
        const shouldPersist = !this.canUseCameraInput() && mode === 'hand';
        if (shouldPersist) {
            mode = 'mouse_pull';
        }

        this.setInputMode(mode, { persist: shouldPersist });
    }

    teardownMouseInput() {
        const canvas = document.querySelector('#canvas');
        if (!canvas || !this._mouseInputHandlers) return;

        const {
            onMouseDown,
            onMouseUp,
            onMouseMove,
            onMouseDownCursor,
            onMouseUpCursor,
        } = this._mouseInputHandlers;

        canvas.removeEventListener('mousedown', onMouseDown);
        canvas.removeEventListener('mouseup', onMouseUp);
        canvas.removeEventListener('mousemove', onMouseMove);
        canvas.removeEventListener('mousedown', onMouseDownCursor);
        canvas.removeEventListener('mouseup', onMouseUpCursor);
        canvas.style.cursor = 'default';
        this._mouseInputHandlers = null;
    }

    setupMouseInput() {
        this.teardownMouseInput();

        const canvas = document.querySelector('#canvas');
        if (!canvas) return;

        const onMouseDown = (e) => {
            if (!this.isMouseInput()) return;
            const gridCoords = this.screenToViewportGrid(...this.getCanvasPointer(canvas, e));
            if (gridCoords) this.module._onMouseDown(gridCoords.x, gridCoords.y);
        };

        const onMouseUp = () => {
            if (!this.isMouseInput()) return;
            this.module._onMouseUp();
        };

        const onMouseMove = (e) => {
            if (!this.isMouseInput() || e.buttons !== 1) return;
            const gridCoords = this.screenToViewportGrid(...this.getCanvasPointer(canvas, e));
            if (gridCoords) this.module._onMouseDrag(gridCoords.x, gridCoords.y);
        };

        const onMouseDownCursor = () => { canvas.style.cursor = 'grabbing'; };
        const onMouseUpCursor = () => { canvas.style.cursor = 'grab'; };

        canvas.addEventListener('mousedown', onMouseDown);
        canvas.addEventListener('mouseup', onMouseUp);
        canvas.addEventListener('mousemove', onMouseMove);
        canvas.style.cursor = 'grab';
        canvas.addEventListener('mousedown', onMouseDownCursor);
        canvas.addEventListener('mouseup', onMouseUpCursor);

        this._mouseInputHandlers = {
            onMouseDown,
            onMouseUp,
            onMouseMove,
            onMouseDownCursor,
            onMouseUpCursor,
        };
    }

    getCanvasPointer(canvas, e) {
        const rect = canvas.getBoundingClientRect();
        return [e.clientX - rect.left, e.clientY - rect.top];
    }

    screenToViewportGrid(canvasX, canvasY) {
        if (!this.layoutPixels) return null;

        const cellSize = this.module._getSimCellSize();
        const domainWidth = this.module._getSimDomainWidth();
        const domainHeight = this.module._getSimDomainHeight();

        for (const name of VIEWPORT_NAMES) {
            const vp = this.layoutPixels[name];
            if (!vp || vp.width <= 0 || vp.height <= 0) continue;

            // Check if click is inside this viewport
            if (canvasX >= vp.x && canvasX < vp.x + vp.width &&
                canvasY >= vp.y && canvasY < vp.y + vp.height) {

                // Mirror WGSL fragment shader transform
                const normX = (canvasX - vp.x) / vp.width;
                const normY = (canvasY - vp.y) / vp.height;
                const simX = normX * domainWidth;
                const simY = (1.0 - normY) * domainHeight;
                const gridX = Math.floor(simX / cellSize);
                const gridY = Math.floor(simY / cellSize);

                return { x: gridX, y: gridY };
            }
        }
        return null; // click was not in any viewport
    }

    setInputMode(mode, { persist = true } = {}) {
        const mouseInput = isMouseInputMode(mode);

        if (persist) {
            const FS = this.module.FS;
            const configText = FS.readFile('/config.json', { encoding: 'utf8' });
            const config = JSON.parse(configText);
            config.inputMode = mode;
            FS.writeFile('/config.json', JSON.stringify(config, null, 4));
            window.kataraConfig = config;
        } else {
            if (!window.kataraConfig) window.kataraConfig = {};
            window.kataraConfig.inputMode = mode;
        }

        this.module._setInputMode(INPUT_MODE_INT[mode] ?? 0);

        if (mouseInput && this.module) {
            this.clearHandTracking();
        }

        this.positionCameraPanel(this.layoutPixels);
        this.updateCameraUi();

        if (mouseInput) {
            this.setupMouseInput();
        } else {
            this.teardownMouseInput();
        }

        if (this.settingsPanel) {
            this.settingsPanel.refresh();
        }
    }
}

class ViewportController {
    static TARGET_COUNT = 8;

    constructor(viewportIndex, viewportName, target, velocity, layoutRect, parentApp) {
        this.viewportIndex = viewportIndex;
        this.viewportName = viewportName;
        this.target = target;
        this.velocity = velocity;
        this.layoutRect = layoutRect;
        this.parentApp = parentApp;
        this.targetButtonElement = null;
        this.velocityButtonElement = null;
        this.screenshotButtonElement = null;
    }

    createTargetButton() {
        const viewport = document.querySelector('.sim-viewport');
        if (!viewport) return;

        // Create screenshot button
        const screenshotBtn = document.createElement('button');
        screenshotBtn.className = 'viewport-screenshot-btn';
        screenshotBtn.innerHTML = `
            <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"></path>
                <circle cx="12" cy="13" r="4"></circle>
            </svg>
        `;
        screenshotBtn.addEventListener('click', () => this.captureScreenshot());
        viewport.appendChild(screenshotBtn);
        this.screenshotButtonElement = screenshotBtn;

        // Create velocity button
        const velocityBtn = document.createElement('button');
        velocityBtn.className = 'viewport-velocity-btn';
        if (!this.velocity) {
            velocityBtn.classList.add('disabled');
        }
        velocityBtn.textContent = 'V';
        velocityBtn.addEventListener('click', () => this.toggleVelocity());
        viewport.appendChild(velocityBtn);
        this.velocityButtonElement = velocityBtn;

        // Create target button
        const targetBtn = document.createElement('button');
        targetBtn.className = 'viewport-target-btn';
        targetBtn.textContent = `${this.target}`;
        targetBtn.addEventListener('click', () => this.cycleTarget());
        viewport.appendChild(targetBtn);
        this.targetButtonElement = targetBtn;

        // Position after elements are rendered so offsetWidth is available
        requestAnimationFrame(() => this.positionButtons());
    }

    positionButtons() {
        if (!this.targetButtonElement || !this.velocityButtonElement || !this.screenshotButtonElement || !this.layoutRect) return;
        const margin = 8;
        const gap = 4;
        const label = this.parentApp.plotLabelElements.get(this.viewportName);

        let topY = this.layoutRect.y + margin;
        if (label && label.offsetHeight > 0 && label.style.display !== 'none') {
            topY = this.layoutRect.y + margin + label.offsetHeight + gap;
        }

        const buttons = [
            this.screenshotButtonElement,
            this.velocityButtonElement,
            this.targetButtonElement
        ];
        let x = this.layoutRect.x + margin;
        for (const btn of buttons) {
            btn.style.left = `${x}px`;
            btn.style.top = `${topY}px`;
            x += measureElementWidth(btn) + gap;
        }
    }

    cycleTarget() {
        this.target = (this.target + 1) % ViewportController.TARGET_COUNT;
        this.updateDisplay();

        // Pass viewport index instead of string (simpler, no string conversion needed)
        this.parentApp.module._setViewportTarget(this.viewportIndex, this.target);

        this.parentApp.initLayoutFromCpp();
    }

    toggleVelocity() {
        this.velocity = !this.velocity;
        this.updateVelocityDisplay();

        // Pass viewport index and enabled state
        this.parentApp.module._setViewportVelocity(this.viewportIndex, this.velocity ? 1 : 0);

        this.parentApp.initLayoutFromCpp();
    }

    updateDisplay() {
        if (this.targetButtonElement) {
            this.targetButtonElement.textContent = `${this.target}`;
            this.positionButtons();
        }
    }

    updateVelocityDisplay() {
        if (this.velocityButtonElement) {
            if (this.velocity) {
                this.velocityButtonElement.classList.remove('disabled');
            } else {
                this.velocityButtonElement.classList.add('disabled');
            }
        }
    }

    updateLayout(layoutRect) {
        this.layoutRect = layoutRect;
        this.positionButtons();
    }

    setButtonsVisible(visible) {
        const display = visible ? '' : 'none';
        for (const btn of [
            this.screenshotButtonElement,
            this.velocityButtonElement,
            this.targetButtonElement
        ]) {
            if (btn) btn.style.display = display;
        }
    }

    destroy() {
        if (this.targetButtonElement?.parentNode) {
            this.targetButtonElement.parentNode.removeChild(this.targetButtonElement);
        }
        if (this.velocityButtonElement?.parentNode) {
            this.velocityButtonElement.parentNode.removeChild(this.velocityButtonElement);
        }
        if (this.screenshotButtonElement?.parentNode) {
            this.screenshotButtonElement.parentNode.removeChild(this.screenshotButtonElement);
        }
        this.targetButtonElement = null;
        this.velocityButtonElement = null;
        this.screenshotButtonElement = null;
    }

    captureScreenshot() {
        const canvas = document.querySelector('#canvas');
        if (!canvas || !this.layoutRect) return;

        // Generate timestamp: katara_YYYYMMDD-HHMMSS.png
        const now = new Date();
        const year = now.getFullYear();
        const month = String(now.getMonth() + 1).padStart(2, '0');
        const day = String(now.getDate()).padStart(2, '0');
        const hours = String(now.getHours()).padStart(2, '0');
        const minutes = String(now.getMinutes()).padStart(2, '0');
        const seconds = String(now.getSeconds()).padStart(2, '0');
        const filename = `katara_${year}${month}${day}-${hours}${minutes}${seconds}.png`;

        // Get the full canvas as a blob
        canvas.toBlob((blob) => {
            if (!blob) {
                console.error('Failed to capture canvas');
                return;
            }

            // Create an image from the blob
            const img = new Image();
            img.onload = () => {
                // Create a temporary canvas sized to the viewport
                const tempCanvas = document.createElement('canvas');
                tempCanvas.width = this.layoutRect.width;
                tempCanvas.height = this.layoutRect.height;
                const ctx = tempCanvas.getContext('2d');

                // Draw only the viewport region from the source image
                ctx.drawImage(
                    img,
                    this.layoutRect.x, this.layoutRect.y, this.layoutRect.width, this.layoutRect.height,  // Source
                    0, 0, this.layoutRect.width, this.layoutRect.height  // Destination
                );

                // Convert to blob and download
                tempCanvas.toBlob((screenshotBlob) => {
                    if (!screenshotBlob) {
                        console.error('Failed to create screenshot blob');
                        return;
                    }

                    // Create download link
                    const url = URL.createObjectURL(screenshotBlob);
                    const a = document.createElement('a');
                    a.href = url;
                    a.download = filename;
                    document.body.appendChild(a);
                    a.click();
                    document.body.removeChild(a);
                    URL.revokeObjectURL(url);

                    console.log('Screenshot saved:', filename);
                }, 'image/png');
            };
            img.src = URL.createObjectURL(blob);
        }, 'image/png');
    }
}

document.addEventListener('DOMContentLoaded', () => {
    const app = new KataraWebApp();
    window.kataraApp = app;
    app.init();
});
