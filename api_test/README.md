# Hand Tracking WASM

Web-based hand tracking system that uses MediaPipe JavaScript for keypoint detection and C++ compiled to WebAssembly (WASM) for rendering.

## Architecture

```
Camera (getUserMedia) → MediaPipe JS API → Direct Function Call → WASM C++ Renderer → Canvas
```

**Important**: This version uses direct function calls instead of `SharedArrayBuffer`, making it compatible with GitHub Pages and other static hosting services that don't support custom headers.

## Features

- **Hand Tracking**: Uses MediaPipe's `@mediapipe/tasks-vision` for 21-keypoint hand detection
- **WASM Rendering**: C++ rendering code compiled to WebAssembly with Emscripten
- **Direct Function Calls**: Data passed via WASM heap allocation (no SharedArrayBuffer needed)
- **GitHub Pages Compatible**: Works on any static hosting service
- **Real-time Performance**: Target 30+ FPS
- **Privacy-First**: All processing happens locally in the browser

## Project Structure

```
api_test/
├── src/
│   ├── cpp/          # C++ source compiled to WASM
│   ├── js/           # JavaScript modules
│   └── web/          # HTML/CSS frontend
├── build/            # Emscripten build config
├── server.py         # HTTP server for local development
└── package.json      # npm dependencies
```

## Requirements

- **Node.js** 16+ and npm
- **Emscripten** 3.1+ (for C++ → WASM compilation)
- **Python** 3.6+ (for local server, optional)
- A modern browser with WebGL 2 support

### Installing Emscripten

See [Emscripten documentation](https://emscripten.org/docs/getting_started/downloads.html).

Quick install via SDK:
```bash
# Clone the emsdk repo
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# Install and activate
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

## Setup

1. **Install npm dependencies:**
   ```bash
   npm install
   ```

2. **Build the WASM module:**
   ```bash
   npm run build:wasm
   ```

   This compiles the C++ code to WASM using Emscripten and copies the output to `src/web/`.

3. **Start the local server (optional):**
   ```bash
   npm run serve
   ```

   Or manually:
   ```bash
   python3 server.py
   ```

4. **Open in browser:**
   ```
   http://localhost:8000
   ```

## Deploying to GitHub Pages

This version is fully compatible with GitHub Pages! After building the WASM module:

1. **Build the project:**
   ```bash
   npm run build:wasm
   ```

2. **Deploy `src/web/` directory to GitHub Pages**

   You can use the `gh-pages` branch or GitHub Actions for automatic deployment.

3. **Access your site:**
   ```
   https://yourusername.github.io/your-repo/
   ```

No special headers or server configuration required!

## How It Works

### 1. Camera Capture (`camera.js`)
Uses `getUserMedia()` to capture video frames from the webcam. Frames are extracted to `ImageData` via `OffscreenCanvas`.

### 2. Hand Detection (`mediapipe_wrapper.js`)
MediaPipe's HandLandmarker processes each frame, detecting up to 2 hands with 21 keypoints each (x, y, z coordinates).

### 3. Data Transfer (`wasm_interface.js`)
Frame data and keypoints are copied to the WASM heap via `HEAPU8.set()` and `_malloc()`. This is slightly slower than SharedArrayBuffer but works everywhere.

### 4. WASM Rendering (`renderer.cpp`)
The C++ renderer reads from WASM memory and renders:
- The camera video frame as a textured quad
- Hand keypoints as colored points (green for left, orange for right)
- Key connections showing hand structure

## Performance Comparison

| Approach | Latency | GitHub Pages | Notes |
|----------|---------|--------------|-------|
| **SharedArrayBuffer** (original) | ~10-20ms | ❌ Requires COOP/COEP headers | Zero-copy, fastest |
| **Direct Calls** (this version) | ~20-40ms | ✅ Works everywhere | Small copy overhead |

The difference is minimal for most use cases. Both approaches run at 30+ FPS.

## Development

- `npm run build:wasm` - Build WASM module
- `npm run serve` - Start development server
- `npm run dev` - Build and serve (combined)

## Troubleshooting

### Camera permission denied

Ensure your browser has permission to access the camera and that you're serving via HTTPS (or localhost).

### WASM module fails to load

1. Ensure you've built the WASM module: `npm run build:wasm`
2. Check that `hand_tracking.js` and `hand_tracking.wasm` exist in `src/web/`
3. Check browser console for specific errors

### CORS errors when loading from CDN

MediaPipe models are loaded from jsdelivr CDN. Some browsers may block this. Consider serving models locally if needed.

## Hosting Options

| Service | Compatible | Notes |
|---------|------------|-------|
| GitHub Pages | ✅ Yes | Works out of the box |
| Netlify | ✅ Yes | No special config needed |
| Vercel | ✅ Yes | No special config needed |
| Cloudflare Pages | ✅ Yes | No special config needed |
| Surge.sh | ✅ Yes | No special config needed |

## References

- [MediaPipe Hand Landmarker](https://ai.google.dev/edge/mediapipe/solutions/vision/hand_landmarker/web_js)
- [Emscripten Documentation](https://emscripten.org/docs/)
- [WebAssembly](https://webassembly.org/)
- [GitHub Pages](https://pages.github.com/)

## License

This project is part of the Katara fluid simulation project.
