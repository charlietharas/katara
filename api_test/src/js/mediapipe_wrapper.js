/**
 * MediaPipe Hand Landmarker integration.
 * Uses the older MediaPipe Hands solution (better CORS support).
 */

export class MediaPipeHandTracker {
    constructor() {
        this.hands = null;
        this.runningMode = 'VIDEO';
        this.numHands = 2;
        this.modelLoaded = false;
        this.onResultsCallback = null;
    }

    /**
     * Initialize the MediaPipe Hands.
     * @returns {Promise<void>}
     */
    async init() {
        try {
            // Dynamically load MediaPipe Hands scripts
            await this.loadScript('https://cdn.jsdelivr.net/npm/@mediapipe/camera_utils/camera_utils.js');
            await this.loadScript('https://cdn.jsdelivr.net/npm/@mediapipe/control_utils/control_utils.js');
            await this.loadScript('https://cdn.jsdelivr.net/npm/@mediapipe/drawing_utils/drawing_utils.js');
            await this.loadScript('https://cdn.jsdelivr.net/npm/@mediapipe/hands/hands.js');

            // Wait for Hands to be available
            await this.waitForGlobal('Hands');

            // Create Hands instance
            this.hands = new Hands({
                locateFile: (file) => {
                    return `https://cdn.jsdelivr.net/npm/@mediapipe/hands/${file}`;
                }
            });

            // Configure Hands
            this.hands.setOptions({
                maxNumHands: 2,
                modelComplexity: 1,
                minDetectionConfidence: 0.5,
                minTrackingConfidence: 0.5
            });

            // Set up results handler (we'll call onResults manually)
            this.hands.onResults((results) => {
                if (this.onResultsCallback) {
                    this.onResultsCallback(results);
                }
            });

            this.modelLoaded = true;
            console.log('MediaPipe Hands initialized');
        } catch (error) {
            console.error('Failed to initialize MediaPipe:', error);
            throw error;
        }
    }

    /**
     * Helper to load a script.
     */
    loadScript(src) {
        return new Promise((resolve, reject) => {
            const script = document.createElement('script');
            script.src = src;
            script.onload = resolve;
            script.onerror = () => reject(new Error(`Failed to load: ${src}`));
            document.head.appendChild(script);
        });
    }

    /**
     * Wait for a global variable to be defined.
     */
    waitForGlobal(name, timeout = 10000) {
        return new Promise((resolve, reject) => {
            if (window[name]) {
                resolve();
                return;
            }

            const interval = setInterval(() => {
                if (window[name]) {
                    clearInterval(interval);
                    resolve();
                }
            }, 100);

            setTimeout(() => {
                clearInterval(interval);
                reject(new Error(`Timeout waiting for ${name}`));
            }, timeout);
        });
    }

    /**
     * Detect hands in a video frame.
     * @param {HTMLVideoElement} videoElement - The video element
     * @returns {Promise<Array>>} Promise resolving to hand detection results
     */
    async detectHands(videoElement) {
        if (!this.hands) {
            console.error('MediaPipe Hands not initialized');
            return [];
        }

        return new Promise((resolve) => {
            this.onResultsCallback = (results) => {
                const normalized = this.normalizeResults(results);
                resolve(normalized);
            };

            this.hands.send({ image: videoElement }).catch((err) => {
                console.error('MediaPipe detection error:', err);
                resolve([]);
            });
        });
    }

    /**
     * Normalize MediaPipe results to format expected by WASM.
     * @param {Object} results - Raw MediaPipe results
     * @returns {Array} Normalized hand data
     */
    normalizeResults(results) {
        if (!results || !results.multiHandLandmarks) {
            return [];
        }

        const hands = [];

        for (let i = 0; i < results.multiHandLandmarks.length; i++) {
            const landmarks = results.multiHandLandmarks[i];
            const handedness = results.multiHandedness && results.multiHandedness[i] ?
                results.multiHandedness[i] : null;

            // MediaPipe returns 21 keypoints, each with x, y, z coordinates
            const keypoints = [];

            for (let j = 0; j < landmarks.length; j++) {
                keypoints.push([
                    landmarks[j].x,
                    landmarks[j].y,
                    landmarks[j].z || 0
                ]);
            }

            hands.push({
                keypoints: keypoints,
                confidence: handedness ? handedness.score : 1.0,
                handedness: handedness ?
                    (handedness.label === 'Left' ? 0 : 1) : 255
            });
        }

        return hands;
    }

    /**
     * Check if the model is loaded and ready.
     * @returns {boolean}
     */
    isReady() {
        return this.modelLoaded && this.hands !== null;
    }

    /**
     * Get the number of hands being tracked.
     * @returns {number}
     */
    getNumHands() {
        return this.numHands;
    }
}

/**
 * Hand landmark indices for reference:
 * 0: Wrist
 * 1-4: Thumb (tip to base)
 * 5-8: Index finger (tip to base)
 * 9-12: Middle finger (tip to base)
 * 13-16: Ring finger (tip to base)
 * 17-20: Pinky (tip to base)
 */
export const HandLandmarkIndices = {
    WRIST: 0,
    THUMB_TIP: 4,
    THUMB_IP: 3,
    THUMB_MCP: 2,
    THUMB_CMC: 1,
    INDEX_TIP: 8,
    INDEX_DIP: 7,
    INDEX_PIP: 6,
    INDEX_MCP: 5,
    MIDDLE_TIP: 12,
    MIDDLE_DIP: 11,
    MIDDLE_PIP: 10,
    MIDDLE_MCP: 9,
    RING_TIP: 16,
    RING_DIP: 15,
    RING_PIP: 14,
    RING_MCP: 13,
    PINKY_TIP: 20,
    PINKY_DIP: 19,
    PINKY_PIP: 18,
    PINKY_MCP: 17
};
