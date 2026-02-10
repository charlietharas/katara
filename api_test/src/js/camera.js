/**
 * Camera handling class for capturing video frames from the user's webcam.
 * Uses getUserMedia API and OffscreenCanvas for efficient frame extraction.
 */
export class Camera {
    constructor(width = 640, height = 480, fps = 30) {
        this.width = width;
        this.height = height;
        this.fps = fps;
        this.stream = null;
        this.videoElement = null;
        this.offscreenCanvas = null;
        this.offscreenCtx = null;
    }

    /**
     * Initialize the camera and request necessary permissions.
     * @returns {Promise<void>}
     */
    async init() {
        const constraints = {
            video: {
                width: { ideal: this.width },
                height: { ideal: this.height },
                frameRate: { ideal: this.fps },
                facingMode: 'user'
            }
        };

        try {
            this.stream = await navigator.mediaDevices.getUserMedia(constraints);
            this.videoElement = document.createElement('video');
            this.videoElement.srcObject = this.stream;
            this.videoElement.autoplay = true;
            this.videoElement.playsInline = true;
            this.videoElement.muted = true;

            // Wait for video to be ready
            await new Promise((resolve) => {
                this.videoElement.onloadedmetadata = () => {
                    this.videoElement.play();
                    resolve();
                };
            });

            // Update actual dimensions
            this.width = this.videoElement.videoWidth;
            this.height = this.videoElement.videoHeight;

            // Create offscreen canvas for frame extraction
            this.offscreenCanvas = new OffscreenCanvas(this.width, this.height);
            this.offscreenCtx = this.offscreenCanvas.getContext('2d', {
                willReadFrequently: true
            });

            console.log(`Camera initialized: ${this.width}x${this.height} @ ${this.fps}fps`);
        } catch (error) {
            console.error('Failed to initialize camera:', error);
            throw error;
        }
    }

    /**
     * Get the current video frame as ImageData.
     * @returns {ImageData}
     */
    getFrame() {
        if (!this.offscreenCtx) {
            throw new Error('Camera not initialized');
        }

        // Draw video frame to offscreen canvas
        this.offscreenCtx.drawImage(this.videoElement, 0, 0, this.width, this.height);

        // Get ImageData (RGBA format)
        return this.offscreenCtx.getImageData(0, 0, this.width, this.height);
    }

    /**
     * Stop the camera and release resources.
     */
    stop() {
        if (this.videoElement) {
            this.videoElement.pause();
            this.videoElement.srcObject = null;
        }

        if (this.stream) {
            this.stream.getTracks().forEach(track => track.stop());
            this.stream = null;
        }

        console.log('Camera stopped');
    }

    /**
     * Check if camera is currently active.
     * @returns {boolean}
     */
    isActive() {
        return this.stream !== null && this.videoElement !== null;
    }
}
