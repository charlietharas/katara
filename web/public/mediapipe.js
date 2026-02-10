/**
 * MediaPipe Hands Tracker
 * Wraps the MediaPipe Hands API for browser-based hand tracking
 */

export class MediaPipeHandTracker {
    constructor() {
        this.hands = null;
        this.videoElement = null;
        this.camera = null;
    }

    /**
     * Initialize the hand tracker with a video element
     * @param {HTMLVideoElement} videoElement - Video element to process
     */
    async init(videoElement) {
        this.videoElement = videoElement;

        // Load MediaPipe Hands dynamically
        await this.loadScript('https://cdn.jsdelivr.net/npm/@mediapipe/hands/hands.js');
        await this.loadScript('https://cdn.jsdelivr.net/npm/@mediapipe/camera_utils/camera_utils.js');
        await this.loadScript('https://cdn.jsdelivr.net/npm/@mediapipe/drawing_utils/drawing_utils.js');

        // Initialize the Hands solution
        // @ts-ignore - MediaPipe is loaded dynamically
        this.hands = new Hands({
            locateFile: (file) => {
                return `https://cdn.jsdelivr.net/npm/@mediapipe/hands/${file}`;
            }
        });

        // Configure hands options
        this.hands.setOptions({
            maxNumHands: 2,
            modelComplexity: 1,
            minDetectionConfidence: 0.5,
            minTrackingConfidence: 0.5
        });

        // Initialize the hands solution
        await this.hands.initialize();
        console.log('MediaPipe Hands initialized');
    }

    /**
     * Detect hands in the current video frame
     * @returns {Promise<{x: number, y: number, present: boolean}>}
     */
    async detectHands() {
        if (!this.hands || !this.videoElement) {
            return { x: 0, y: 0, present: false };
        }

        try {
            const results = await this.hands.send({ image: this.videoElement });

            if (!results.multiHandLandmarks || results.multiHandLandmarks.length === 0) {
                return { x: 0, y: 0, present: false };
            }

            // Find right hand (or use first hand if no handedness info)
            let handIndex = 0;
            if (results.multiHandedness && results.multiHandedness.length > 0) {
                const rightIdx = results.multiHandedness.findIndex(h => h.label === 'Right');
                if (rightIdx !== -1) {
                    handIndex = rightIdx;
                }
            }

            // Get index finger tip (landmark 8)
            const tip = results.multiHandLandmarks[handIndex][8];

            return {
                x: tip.x,
                y: tip.y,
                present: true
            };
        } catch (error) {
            console.error('Hand detection error:', error);
            return { x: 0, y: 0, present: false };
        }
    }

    /**
     * Load a script dynamically
     * @param {string} src - Script URL
     * @returns {Promise<void>}
     */
    loadScript(src) {
        return new Promise((resolve, reject) => {
            // Check if script already exists
            if (document.querySelector(`script[src="${src}"]`)) {
                resolve();
                return;
            }

            const script = document.createElement('script');
            script.src = src;
            script.onload = () => resolve();
            script.onerror = () => reject(new Error(`Failed to load script: ${src}`));
            document.head.appendChild(script);
        });
    }
}
