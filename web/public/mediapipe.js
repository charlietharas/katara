export class MediaPipeHandTracker {
    async init(videoElement) {
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
        if (!this.hands || !this.videoElement) {
            return { fingertips: [], hands: [] };
        }

        // send the image and wait for results
        await new Promise(resolve => {
            this.resultsReady = resolve;
            this.hands.send({image: this.videoElement});
        });

        if (!this.lastResults) {
            return { fingertips: [], hands: [] };
        }

        if (!this.lastResults.multiHandLandmarks || this.lastResults.multiHandLandmarks.length === 0) {
            return { fingertips: [], hands: [] };
        }

        // build hands array with full landmark data
        const hands = [];
        const handedness = this.lastResults.multiHandedness || [];

        for (let i = 0; i < this.lastResults.multiHandLandmarks.length; i++) {
            const landmarks = this.lastResults.multiHandLandmarks[i];
            const handLabel = handedness[i]?.label ?? 'Unknown';
            hands.push({
                landmarks: landmarks.map(lm => ({ x: lm.x, y: lm.y, z: lm.z })),
                handedness: handLabel,
                confidence: handedness[i]?.score ?? 1.0
            });
        }

        // line segment mode needs all landmarks as a flat array
        const landmarks = [];

        for (let handIdx = 0; handIdx < Math.min(hands.length, 2); handIdx++) {
            const hand = hands[handIdx];
            if (!hand || !hand.landmarks || hand.landmarks.length === 0) {
                // add empty landmarks for this hand
                for (let i = 0; i < 21; i++) {
                    landmarks.push({ x: 0, y: 0, z: 0, present: false });
                }
                continue;
            }

            // add all 21 landmarks for this hand
            for (const lm of hand.landmarks) {
                landmarks.push({
                    x: lm.x,
                    y: lm.y,
                    z: lm.z,
                    present: true
                });
            }
        }

        // pad to 42 landmarks (2 hands * 21 landmarks)
        while (landmarks.length < 42) {
            landmarks.push({ x: 0, y: 0, z: 0, present: false });
        }

        return { landmarks, hands };
    }

    loadScript(src) {
        return new Promise((resolve, reject) => {
            // check if already loaded
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
