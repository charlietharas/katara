import { MediaPipeHandTracker } from './mediapipe.js';

class KataraWebApp {
    getSimViewportSize() {
        const viewport = document.querySelector('.sim-viewport');
        if (!viewport) {
            throw new Error('Sim viewport not found');
        }
        return {
            width: viewport.clientWidth,
            height: viewport.clientHeight
        };
    }

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
        const size = this.getSimViewportSize();
        simCanvas.width = size.width;
        simCanvas.height = size.height;
        this.syncLayoutStateFromConfig();
        const layout = this.initLayoutFromCpp();
        this.positionCameraPanel(layout);
        this.positionControls(layout);
    }

    async init() {
        this.isInkMode = false;
        this.inkAspectRatio = 1.0;
        this.cameraAspectRatio = 4.0 / 3.0;

        // Show loading overlay
        const loadingOverlay = document.getElementById('loadingOverlay');
        if (loadingOverlay) {
            loadingOverlay.classList.remove('hidden');
        }

        await this.waitForNav();

        // Canvas size will be updated after C++ resizes window
        const simCanvas = document.querySelector('#canvas');
        const initialSize = this.getSimViewportSize();
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
            await new Promise((resolve) => {
                FS.syncfs(true, (err) => {
                    if (err) console.error('syncfs failed:', err);
                    else console.log('syncfs completed');
                    resolve();
                });
            });

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
        const settledSize = this.getSimViewportSize();
        simCanvas.width = settledSize.width;
        simCanvas.height = settledSize.height;
        console.log('After C++ init: canvas size', simCanvas.width, 'x', simCanvas.height);
        console.log('Window AR:', (settledSize.width / settledSize.height).toFixed(3));
        const initialLayout = this.initLayoutFromCpp();
        this.positionControls(initialLayout);
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

        // Recompute after camera metadata is ready so camera frame can use stream AR.
        const cameraAlignedLayout = this.initLayoutFromCpp();
        this.positionCameraPanel(cameraAlignedLayout);
        this.positionControls(cameraAlignedLayout);

        this.setupInkUpload();
        this.setupCameraCapture();
        this.setupResetButton();

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

    async setupCamera() {
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

        // Adjust camera canvas to match camera stream aspect ratio
        const videoWidth = this.videoElement.videoWidth;
        const videoHeight = this.videoElement.videoHeight;
        console.log('Camera stream dimensions:', videoWidth, videoHeight);

        if (videoWidth && videoHeight) {
            this.cameraCanvas.width = videoWidth;
            this.cameraCanvas.height = videoHeight;
            this.cameraAspectRatio = videoWidth / videoHeight;
        }
    }

    initLayoutFromCpp() {
        const { width: canvasW, height: canvasH } = this.getSimViewportSize();
        this.module._initLayout(canvasW, canvasH, this.inkAspectRatio, this.cameraAspectRatio);
        const jsonStr = this.module.FS.readFile('/layout_pixels.json', { encoding: 'utf8' });
        this.layoutPixels = JSON.parse(jsonStr);
        console.log('Layout from C++:', canvasW, 'x', canvasH, this.layoutPixels);
        return this.layoutPixels;
    }

    syncLayoutStateFromConfig() {
        if (!this.module || !this.module.FS) return;

        try {
            const configText = this.module.FS.readFile('/config.json', { encoding: 'utf8' });
            const config = JSON.parse(configText);
            this.isInkMode = config?.rendering?.target === 3;
        } catch (err) {
            console.warn('Could not read /config.json for layout mode:', err);
        }
    }

    async updateInkAspectRatioFromConfig() {
        if (!this.module || !this.module.FS) return;

        let config;
        try {
            const configText = this.module.FS.readFile('/config.json', { encoding: 'utf8' });
            config = JSON.parse(configText);
        } catch (err) {
            console.warn('Could not parse /config.json for ink AR:', err);
            return;
        }

        if (config?.rendering?.target !== 3) return;

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
        const camFrame = layout.camera_frame;
        const camPanel = document.querySelector('.camera-panel');
        if (camFrame && camPanel && this.cameraCanvas) {
            camPanel.style.position = 'absolute';
            camPanel.style.left = camFrame.x + 'px';
            camPanel.style.top = camFrame.y + 'px';
            camPanel.style.width = camFrame.width + 'px';
            camPanel.style.height = camFrame.height + 'px';

            this.cameraCanvas.style.width = '100%';
            this.cameraCanvas.style.height = '100%';
            this.cameraCanvas.style.maxWidth = 'none';
            console.log('Camera positioned from layout:', camFrame.x, camFrame.y, camFrame.width, 'x', camFrame.height);
        }
    }

    getGridBounds(layout) {
        if (!layout) return null;

        const viewportNames = ['viewport_1', 'viewport_2', 'viewport_3'];
        const viewportRects = viewportNames
            .map(name => layout[name])
            .filter(rect => rect && Number.isFinite(rect.x) && Number.isFinite(rect.y) &&
                Number.isFinite(rect.width) && Number.isFinite(rect.height));

        const candidateRects = viewportRects.length > 0
            ? viewportRects
            : Object.values(layout).filter(rect =>
                rect && Number.isFinite(rect.x) && Number.isFinite(rect.y) &&
                Number.isFinite(rect.width) && Number.isFinite(rect.height));

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
        const targetLeft = bounds.x + bounds.width - controls.offsetWidth - margin;
        const targetTop = bounds.y + bounds.height - controls.offsetHeight - margin;

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
            await new Promise((resolve) => {
                FS.syncfs(false, (err) => {
                    if (err) console.error('syncfs failed:', err);
                    else console.log('syncfs completed');
                    resolve();
                });
            });

            // Set flag so on reload we restore from /persist
            localStorage.setItem('katara_needs_restore', 'true');

            // Reload config without page reload (INK + RENDER flags)
            console.log('Reloading config...');
            this.module._reloadConfig(1 | 4);
            this.syncLayoutStateFromConfig();
            const layout = this.initLayoutFromCpp();
            this.positionCameraPanel(layout);
            this.positionControls(layout);

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

        cheeseBtn.addEventListener('click', () => this.captureFromCamera());
    }

    setupResetButton() {
        const resetBtn = document.getElementById('resetBtn');
        if (!resetBtn) {
            console.error('Reset button not found');
            return;
        }

        resetBtn.addEventListener('click', () => {
            if (this.module && this.module._resetFluidField) {
                this.module._resetFluidField();
            } else {
                console.error('resetFluidField not available');
            }
        });
    }

    async captureFromCamera() {
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

    // hand skeleton connections (MediaPipe topology)
    static HAND_CONNECTIONS = [
        // thumb
        [0, 1], [1, 2], [2, 3], [3, 4],
        // index
        [0, 5], [5, 6], [6, 7], [7, 8],
        // middle
        [0, 9], [9, 10], [10, 11], [11, 12],
        // ring
        [0, 13], [13, 14], [14, 15], [15, 16],
        // pinky
        [0, 17], [17, 18], [18, 19], [19, 20],
        // palm
        [5, 9], [9, 13], [13, 17]
    ];

    drawKeypoints(hands) {
        const ctx = this.cameraCtx;
        const width = this.cameraCanvas.width;
        const height = this.cameraCanvas.height;

        for (const hand of hands) {
            // left/right hand get different colors
            const isLeftHand = hand.handedness === 'Right';
            const color = isLeftHand ? '#00ff88' : '#ff9933';

            // skeleton connections
            ctx.strokeStyle = color;
            ctx.lineWidth = 3;
            for (const [i, j] of KataraWebApp.HAND_CONNECTIONS) {
                const p1 = hand.landmarks[i];
                const p2 = hand.landmarks[j];
                ctx.beginPath();
                ctx.moveTo(p1.x * width, p1.y * height);
                ctx.lineTo(p2.x * width, p2.y * height);
                ctx.stroke();
            }

            // keypoints
            ctx.fillStyle = color;
            for (const lm of hand.landmarks) {
                ctx.beginPath();
                ctx.arc(lm.x * width, lm.y * height, 5, 0, 2 * Math.PI);
                ctx.fill();
            }
        }
    }

    async processLoop() {
        try {
            const video = this.videoElement;
            if (!(video instanceof HTMLVideoElement) || video.readyState < 2 || video.videoWidth === 0) {
                requestAnimationFrame(() => this.processLoop());
                return;
            }

            // detect hands and get full landmark data with all 42 landmarks
            const result = await this.handTracker.detectHands();

            // camera frame to canvas
            this.cameraCtx.drawImage(video, 0, 0, this.cameraCanvas.width, this.cameraCanvas.height);

            // keypoints overlay
            if (result.hands && result.hands.length > 0) {
                this.drawKeypoints(result.hands);
            }

            // update simulation with line segments between landmarks
            // circle momentum transfer gets all 42 landmarks (21 per hand)
            const allLandmarks = result.landmarks;
            if (allLandmarks && allLandmarks.length > 0) {
                // allocate memory for all landmarks (used for both circles and line segments)
                const allLandmarksDataLength = allLandmarks.length * 4;
                const ptrAll = this.module._malloc(allLandmarksDataLength * 4);

                const heap = this.module.HEAPF32;

                // copy all landmark data
                let offset = ptrAll / 4;
                for (let i = 0; i < allLandmarks.length; i++) {
                    const lm = allLandmarks[i];
                    heap[offset++] = 1.0 - lm.x;  // horizontal reflection
                    heap[offset++] = lm.y;
                    heap[offset++] = lm.z;
                    heap[offset++] = lm.present ? 1.0 : 0.0;  // use actual presence
                }

                // C++ calls
                this.module._updateFingertips(ptrAll, allLandmarks.length);
                this.module._updateLineSegments(ptrAll, allLandmarks.length);

                this.module._free(ptrAll);

                this.frameCount++;
            } else {
                // no hand detected
                this.module._updateFingertips(0, 0);
                this.module._updateLineSegments(0, 0);
            }

        } catch (err) {
            // keep render loop alive even if hand tracking has a transient failure
            console.error('Hand tracking error:', err);
            // don't send empty fingertips on err
        }

        requestAnimationFrame(() => this.processLoop());
    }
}

document.addEventListener('DOMContentLoaded', () => new KataraWebApp().init());
