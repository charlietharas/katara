import { MediaPipeHandTracker } from './mediapipe.js';

class KataraWebApp {
    async init() {
        const status = document.getElementById('status');
        status.textContent = 'Loading WASM module...';

        // Setup simulation canvas first (needed for WASM module)
        const simCanvas = document.querySelector('#canvas');
        simCanvas.width = 1200;
        simCanvas.height = 800;

        // Load WASM module and tell it to use the simulation canvas
        this.module = await createKataraModule({
            canvas: simCanvas
        });

        // Setup camera canvas (2D context for camera + keypoints)
        this.cameraCanvas = document.querySelector('#cameraCanvas');
        if (!this.cameraCanvas) {
            throw new Error('Camera canvas not found');
        }
        this.cameraCtx = this.cameraCanvas.getContext('2d');
        if (!this.cameraCtx) {
            throw new Error('Could not get 2D context for camera canvas');
        }

        status.textContent = 'Initializing hand tracking...';

        // Initialize hand tracking
        this.module._initHandTracking(simCanvas.width, simCanvas.height);

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

    // Hand skeleton connections (MediaPipe topology)
    static HAND_CONNECTIONS = [
        // Thumb
        [0, 1], [1, 2], [2, 3], [3, 4],
        // Index finger
        [0, 5], [5, 6], [6, 7], [7, 8],
        // Middle finger
        [0, 9], [9, 10], [10, 11], [11, 12],
        // Ring finger
        [0, 13], [13, 14], [14, 15], [15, 16],
        // Pinky
        [0, 17], [17, 18], [18, 19], [19, 20],
        // Palm
        [5, 9], [9, 13], [13, 17]
    ];

    drawKeypoints(hands) {
        const ctx = this.cameraCtx;
        const width = this.cameraCanvas.width;
        const height = this.cameraCanvas.height;

        for (const hand of hands) {
            // Color based on handedness (MediaPipe labels are from camera's perspective)
            // "Left" in MediaPipe = user's right hand (mirrored)
            const isLeftHand = hand.handedness === 'Right'; // Mirrored
            const color = isLeftHand ? '#00ff88' : '#ff9933';

            // Draw skeleton connections
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

            // Draw keypoints
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
            // Detect hands and get full landmark data
            const result = await this.handTracker.detectHands();

            // Draw camera frame to canvas
            this.cameraCtx.drawImage(this.videoElement, 0, 0, this.cameraCanvas.width, this.cameraCanvas.height);

            // Draw keypoints overlay
            if (result.hands.length > 0) {
                this.drawKeypoints(result.hands);
            }

            // Update hand position for simulation
            this.module._updateHandPosition(
                result.indexTip.x,
                result.indexTip.y,
                result.indexTip.present
            );
        } catch (err) {
            // Keep render loop alive even if hand tracking has a transient failure.
            console.error('Hand tracking error:', err);
            this.module._updateHandPosition(0, 0, false);
        }
        requestAnimationFrame(() => this.processLoop());
    }
}

document.addEventListener('DOMContentLoaded', () => new KataraWebApp().init());
