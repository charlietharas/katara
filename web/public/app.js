import { MediaPipeHandTracker } from './mediapipe.js';

class KataraWebApp {
    async init() {
        const status = document.getElementById('status');
        status.textContent = 'Loading WASM module...';

        // Load WASM module
        this.module = await createKataraModule();

        // Setup canvas
        const canvas = document.querySelector('#canvas');
        canvas.width = 1200;
        canvas.height = 800;

        status.textContent = 'Initializing hand tracking...';

        // Initialize hand tracking
        this.module._initHandTracking(canvas.width, canvas.height);

        // Setup camera
        await this.setupCamera();

        // Initialize MediaPipe
        this.handTracker = new MediaPipeHandTracker();
        await this.handTracker.init(this.videoElement);

        status.textContent = 'Running! Show your hand to the camera.';

        // Start hand tracking loop
        this.processLoop();
    }

    async setupCamera() {
        const stream = await navigator.mediaDevices.getUserMedia({
            video: { width: 640, height: 480, facingMode: 'user' }
        });

        this.videoElement = document.createElement('video');
        this.videoElement.srcObject = stream;
        this.videoElement.play();
        await new Promise(resolve => this.videoElement.onloadedmetadata = resolve);
    }

    async processLoop() {
        try {
            const pos = await this.handTracker.detectHands();
            this.module._updateHandPosition(pos.x, pos.y, pos.present);
        } catch (err) {
            // Keep render loop alive even if hand tracking has a transient failure.
            console.error('Hand tracking error:', err);
            this.module._updateHandPosition(0, 0, false);
        }
        requestAnimationFrame(() => this.processLoop());
    }
}

document.addEventListener('DOMContentLoaded', () => new KataraWebApp().init());
