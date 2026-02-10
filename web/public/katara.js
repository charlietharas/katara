/**
 * Katara Web Application
 * Main controller for the browser-based fluid simulation
 */

import { MediaPipeHandTracker } from './mediapipe.js';

class KataraWebApp {
    constructor() {
        this.module = null;
        this.canvas = null;
        this.videoElement = null;
        this.handTracker = null;
        this.isRunning = false;
    }

    /**
     * Initialize the application
     */
    async init() {
        console.log('Initializing Katara Web App...');

        // Get canvas
        this.canvas = document.querySelector('#canvas');
        if (!this.canvas) {
            throw new Error('Canvas element not found');
        }

        // Set canvas size
        this.canvas.width = 800;
        this.canvas.height = 600;

        // Load WASM module
        console.log('Loading WASM module...');
        this.module = await createKataraModule({
            canvas: this.canvas,
            locateFile: (file) => {
                if (file.endsWith('.wasm')) {
                    return 'katara.wasm';
                }
                return file;
            }
        });
        console.log('WASM module loaded');

        // Initialize hand tracking
        await this.module._initHandTracking(this.canvas.width, this.canvas.height);

        // Setup camera
        await this.setupCamera();

        // Initialize hand tracker
        this.handTracker = new MediaPipeHandTracker();
        await this.handTracker.init(this.videoElement);
        console.log('Hand tracking initialized');

        // Start the processing loop
        this.isRunning = true;
        this.processLoop();

        console.log('Katara Web App initialized');
    }

    /**
     * Setup camera for hand tracking
     */
    async setupCamera() {
        try {
            const stream = await navigator.mediaDevices.getUserMedia({
                video: {
                    width: { ideal: 640 },
                    height: { ideal: 480 },
                    facingMode: 'user'
                }
            });

            this.videoElement = document.createElement('video');
            this.videoElement.srcObject = stream;
            this.videoElement.playsInline = true;
            this.videoElement.muted = true;

            await new Promise((resolve) => {
                this.videoElement.onloadedmetadata = () => {
                    resolve();
                };
            });

            this.videoElement.play();
            console.log('Camera setup complete');
        } catch (error) {
            console.error('Camera setup failed:', error);
            console.warn('Hand tracking will not be available');
            // Create a dummy video element for graceful degradation
            this.videoElement = document.createElement('video');
        }
    }

    /**
     * Main processing loop - handles hand tracking and updates simulation
     */
    async processLoop() {
        if (!this.isRunning) {
            return;
        }

        try {
            // Detect hand position
            const pos = await this.handTracker.detectHands();

            // Update simulation with hand position
            this.module._updateHandPosition(pos.x, pos.y, pos.present);
        } catch (error) {
            console.error('Process loop error:', error);
        }

        // Schedule next frame
        requestAnimationFrame(() => this.processLoop());
    }

    /**
     * Cleanup on shutdown
     */
    shutdown() {
        this.isRunning = false;

        // Stop camera stream
        if (this.videoElement && this.videoElement.srcObject) {
            const tracks = this.videoElement.srcObject.getTracks();
            tracks.forEach(track => track.stop());
        }
    }
}

// Initialize on DOM ready
let app = null;

document.addEventListener('DOMContentLoaded', async () => {
    // Check for WebGPU support
    if (!navigator.gpu) {
        const message = document.createElement('div');
        message.style.cssText = `
            position: fixed;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            background: #333;
            color: white;
            padding: 20px;
            border-radius: 8px;
            text-align: center;
            font-family: sans-serif;
        `;
        message.innerHTML = `
            <h2>WebGPU Not Supported</h2>
            <p>Your browser doesn't support WebGPU.</p>
            <p>Please use Chrome 113+ or Edge 113+ with WebGPU enabled.</p>
        `;
        document.body.appendChild(message);
        return;
    }

    try {
        app = new KataraWebApp();
        await app.init();
    } catch (error) {
        console.error('Failed to initialize Katara:', error);

        const errorDiv = document.createElement('div');
        errorDiv.style.cssText = `
            position: fixed;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            background: #8b0000;
            color: white;
            padding: 20px;
            border-radius: 8px;
            text-align: center;
            font-family: sans-serif;
        `;
        errorDiv.innerHTML = `
            <h2>Initialization Failed</h2>
            <p>${error.message}</p>
            <p>Check the browser console for details.</p>
        `;
        document.body.appendChild(errorDiv);
    }
});

// Export for external access if needed
window.KataraWebApp = KataraWebApp;
