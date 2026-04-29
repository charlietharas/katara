import { MediaPipeHandTracker } from './mediapipe.js';

class KataraWebApp {
    async init() {
        const simCanvas = document.querySelector('#canvas');
        simCanvas.width = 1200;
        simCanvas.height = 800;

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

        module.callMain([]);

        // 2D context for camera + keypoints
        this.cameraCanvas = document.querySelector('#cameraCanvas');
        if (!this.cameraCanvas) {
            throw new Error('Camera canvas not found');
        }
        this.cameraCtx = this.cameraCanvas.getContext('2d');
        if (!this.cameraCtx) {
            throw new Error('Could not get 2D context for camera canvas');
        }

        await this.setupCamera();
        this.setupInkUpload();

        // mediapipe
        this.handTracker = new MediaPipeHandTracker();
        await this.handTracker.init(this.videoElement);

        // start hand tracking loop
        console.log("Hand tracking starting. Say hi!");
        this.processLoop();
    }

    async setupCamera() {
        const stream = await navigator.mediaDevices.getUserMedia({
            video: { width: 320, height: 240, facingMode: 'user' }
        });

        this.videoElement = document.createElement('video');
        this.videoElement.muted = true;
        this.videoElement.playsInline = true;
        this.videoElement.setAttribute('playsinline', '');
        this.videoElement.srcObject = stream;
        document.body.appendChild(this.videoElement);
        this.videoElement.play();
        await new Promise(resolve => this.videoElement.onloadeddata = resolve);
    }

    setupInkUpload() {
        const uploadBtn = document.getElementById('uploadInkBtn');
        const fileInput = document.getElementById('inkImageInput');

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
            // Read file as ArrayBuffer
            const arrayBuffer = await file.arrayBuffer();
            const uint8Array = new Uint8Array(arrayBuffer);
            console.log('Image loaded: ' + uint8Array.length + ' bytes');

            // Write to /persist
            FS.writeFile('/persist/uploaded.png', uint8Array);

            // Update config to use uploaded image and enable ink mode
            const configText = FS.readFile('/config.json', { encoding: 'utf8' });
            const config = JSON.parse(configText);
            config.ink.imagePath = '/uploaded.png';
            config.rendering.target = 3;
            const newConfigText = JSON.stringify(config, null, 4);

            // Write config to /persist and /
            FS.writeFile('/persist/config.json', newConfigText);
            FS.writeFile('/config.json', newConfigText);

            console.log('Files written to /persist, syncing to IndexedDB...');

            // Sync to IndexedDB
            await new Promise((resolve) => {
                FS.syncfs(false, (err) => {
                    if (err) console.error('syncfs failed:', err);
                    else console.log('syncfs completed - data persisted to IndexedDB');
                    resolve();
                });
            });

            // Set flag so on reload we restore from /persist
            localStorage.setItem('katara_needs_restore', 'true');

            // Reload page to apply changes
            console.log('Ink image uploaded. Reloading...');
            location.reload();

        } catch (err) {
            console.error('Failed to upload image:', err);
            alert('Failed to upload image: ' + err.message);
        }
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
