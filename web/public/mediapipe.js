export class MediaPipeHandTracker {
    async init(videoElement) {
        // Load MediaPipe Hands dynamically
        await this.loadScript('https://cdn.jsdelivr.net/npm/@mediapipe/hands/hands.js');
        await this.loadScript('https://cdn.jsdelivr.net/npm/@mediapipe/camera_utils/camera_utils.js');
        await this.loadScript('https://cdn.jsdelivr.net/npm/@mediapipe/drawing_utils/drawing_utils.js');

        this.lastResults = null;
        this.resultsReady = null;

        this.hands = new Hands({
            locateFile: (file) => `https://cdn.jsdelivr.net/npm/@mediapipe/hands/${file}`
        });

        this.hands.setOptions({
            maxNumHands: 2,
            modelComplexity: 1,
            minDetectionConfidence: 0.5,
            minTrackingConfidence: 0.5
        });

        this.hands.onResults((results) => {
            this.lastResults = results;
            if (this.resultsReady) {
                this.resultsReady(results);
                this.resultsReady = null;
            }
        });

        await this.hands.initialize();
        this.videoElement = videoElement;
    }

    async detectHands() {
        if (!this.hands || !this.videoElement) return { x: 0, y: 0, present: false };

        // Send the image and wait for results
        await new Promise(resolve => {
            this.resultsReady = resolve;
            this.hands.send({image: this.videoElement});
        });

        if (!this.lastResults) {
            return { x: 0, y: 0, present: false };
        }

        if (!this.lastResults.multiHandLandmarks || this.lastResults.multiHandLandmarks.length === 0) {
            return { x: 0, y: 0, present: false };
        }

        // Prefer right hand, fallback to first detected hand.
        const handedness = this.lastResults.multiHandedness || [];
        const rightIdx = handedness.findIndex((h) => {
            const label = h?.label ?? h?.[0]?.label;
            return label === 'Right';
        });
        const handIdx = rightIdx >= 0 ? rightIdx : 0;
        const landmarks = this.lastResults.multiHandLandmarks[handIdx];
        const tip = landmarks?.[8]; // Index finger tip
        if (!tip) {
            return { x: 0, y: 0, present: false };
        }

        return { x: tip.x, y: tip.y, present: true };
    }

    loadScript(src) {
        return new Promise((resolve, reject) => {
            // Check if already loaded
            if (document.querySelector(`script[src="${src}"]`)) {
                resolve();
                return;
            }
            const script = document.createElement('script');
            script.src = src;
            script.onload = resolve;
            script.onerror = reject;
            document.head.appendChild(script);
        });
    }
}
