/**
 * Main application controller for the hand tracking visualization.
 * Coordinates camera capture, MediaPipe detection, and WASM rendering.
 * Uses direct function calls (no SharedArrayBuffer) for GitHub Pages compatibility.
 */

import { Camera } from './camera.js';
import { MediaPipeHandTracker } from './mediapipe_wrapper.js';
import { WasmModule } from './wasm_interface.js';

class HandTrackingApp {
    constructor() {
        this.camera = new Camera(640, 480, 30);
        this.handTracker = new MediaPipeHandTracker();
        this.wasmModule = new WasmModule();

        this.running = false;
        this.frameCount = 0;
        this.lastTime = performance.now();
        this.fps = 0;
        this.animationId = null;

        this.setupUI();
    }

    /**
     * Initialize all components.
     */
    async init() {
        try {
            this.updateStatus('Loading WASM module...');

            // Load and initialize WASM module
            await this.wasmModule.load('./hand_tracking.js');

            // Initialize WASM
            this.wasmModule.init('#canvas');

            this.updateStatus('Initializing camera...');

            // Initialize camera
            await this.camera.init();

            this.updateStatus('Loading MediaPipe models...');

            // Initialize MediaPipe
            await this.handTracker.init();

            this.updateStatus('Ready');

            // Enable start button
            document.getElementById('startButton').disabled = false;

            console.log('HandTrackingApp initialized');

        } catch (error) {
            console.error('Initialization error:', error);
            this.updateStatus('Initialization failed: ' + error.message);
        }
    }

    /**
     * Start the main processing loop.
     */
    async start() {
        if (this.running) return;

        try {
            this.running = true;
            this.frameCount = 0;
            this.lastTime = performance.now();

            // Update UI
            document.getElementById('startButton').disabled = true;
            document.getElementById('stopButton').disabled = false;

            this.updateStatus('Running');

            // Start processing loop
            this.processLoop();

        } catch (error) {
            console.error('Start error:', error);
            this.updateStatus('Start failed: ' + error.message);
            this.stop();
        }
    }

    /**
     * Stop the processing loop.
     */
    stop() {
        this.running = false;

        if (this.animationId) {
            cancelAnimationFrame(this.animationId);
            this.animationId = null;
        }

        // Update UI
        document.getElementById('startButton').disabled = false;
        document.getElementById('stopButton').disabled = true;

        this.updateStatus('Stopped');
        this.updatePerformance(0, 0, 0);
    }

    /**
     * Main processing loop.
     */
    async processLoop() {
        if (!this.running) return;

        const startTime = performance.now();

        try {
            // 1. Get camera frame (for WASM rendering)
            const frame = this.camera.getFrame();

            // 2. Detect hands with MediaPipe (pass video element directly)
            const videoElement = this.camera.videoElement;
            const hands = await this.handTracker.detectHands(videoElement);

            // 3. Process frame in WASM (copy data and render)
            this.wasmModule.processFrame(
                frame.data,      // Uint8ClampedArray with RGBA data
                frame.width,
                frame.height,
                hands            // Array of hand detection results
            );

            // 4. Update performance metrics
            const endTime = performance.now();
            const latency = endTime - startTime;
            const numHands = hands.length;

            // Calculate FPS
            const now = performance.now();
            const delta = now - this.lastTime;
            if (delta >= 1000) {
                this.fps = Math.round((this.frameCount * 1000) / delta);
                this.frameCount = 0;
                this.lastTime = now;
            }

            this.updatePerformance(this.fps, latency, numHands);
            this.frameCount++;

            // Request next frame
            this.animationId = requestAnimationFrame(() => this.processLoop());

        } catch (error) {
            console.error('Processing error:', error);
            this.updateStatus('Error: ' + error.message);
            this.stop();
        }
    }

    /**
     * Set up UI event handlers.
     */
    setupUI() {
        document.getElementById('startButton').disabled = true;
        document.getElementById('stopButton').disabled = true;

        document.getElementById('startButton').addEventListener('click', () => {
            this.start();
        });

        document.getElementById('stopButton').addEventListener('click', () => {
            this.stop();
        });

        // Handle window resize
        window.addEventListener('resize', () => {
            // Could adjust canvas size here if needed
        });
    }

    /**
     * Update the status message.
     */
    updateStatus(message) {
        const statusEl = document.getElementById('status');
        if (statusEl) {
            statusEl.textContent = message;
        }
    }

    /**
     * Update performance metrics display.
     */
    updatePerformance(fps, latency, numHands) {
        const fpsEl = document.getElementById('fps');
        const latencyEl = document.getElementById('latency');
        const handsEl = document.getElementById('hands');

        if (fpsEl) fpsEl.textContent = `FPS: ${fps}`;
        if (latencyEl) latencyEl.textContent = `Latency: ${latency.toFixed(1)}ms`;
        if (handsEl) handsEl.textContent = `Hands: ${numHands}`;
    }

    /**
     * Clean up resources.
     */
    cleanup() {
        this.stop();
        this.camera.stop();
        this.wasmModule.cleanup();
        this.updateStatus('Cleaned up');
    }
}

// Create and initialize the app when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        const app = new HandTrackingApp();
        app.init();

        // Expose for debugging
        window.handTrackingApp = app;
    });
} else {
    const app = new HandTrackingApp();
    app.init();
    window.handTrackingApp = app;
}

export { HandTrackingApp };
