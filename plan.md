# Port Katara to Browser with Hand Tracking Control

## Goal
Port the entire Katara fluid simulation to run in the browser, controlled by hand tracking (right hand index finger tip replaces mouse input for the draggable circle).

## Working Directory
`/home/charlie/code/katara.worktrees/browser-port/` - This is a git worktree (clone) of the main Katara repository.

## Complexity Assessment: **Medium**
**Key finding**: SDL2 has official Emscripten support! sdl2webgpu already has `#ifdef SDL_VIDEO_DRIVER_EMSCRIPTEN` using `WGPUSurfaceDescriptorFromCanvasHTMLSelector`. We don't need to remove SDL2 - just adapt the main loop.

---

## Implementation Plan

### Phase 1: Emscripten Build System

Create `web/` directory structure:
```
web/
├── CMakeLists.txt       # Emscripten build config
├── build/               # Build output
└── public/
    ├── index.html       # Web frontend
    ├── katara.js        # Main app controller
    ├── mediapipe.js     # Hand tracking wrapper
    └── styles.css       # Minimal styling
```

**New: `/home/charlie/code/katara/web/CMakeLists.txt`**
```cmake
cmake_minimum_required(VERSION 3.20)
project(katara_web CXX)

# Source files
set(SOURCES
    ../main.cpp
    ../gpu_render.cpp
    ../gpu_sim.cpp
    ../render.cpp
    ../sim.cpp
    ../config.cpp
    ../webgpu/sdl2webgpu-main/sdl2webgpu.c
)

add_executable(katara ${SOURCES})

# Emscripten flags
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s WASM=1")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s USE_WEBGPU=1")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s ALLOW_MEMORY_GROWTH=1")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s MAXIMUM_MEMORY=4GB")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s ASYNCIFY")

# Exports
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s EXPORTED_FUNCTIONS=[\\\"_main\\\",\\\"_initHandTracking\\\",\\\"_updateHandPosition\\\",\\\"_setSimulatorPointer\\\"]")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s EXPORTED_RUNTIME_METHODS=[\\\"ccall\\\",\\\"cwrap\\\"]")

# Module
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s MODULARIZE=1")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s EXPORT_NAME=\\\"createKataraModule\\\"")

# Embed data
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --embed-file ../shaders@/shaders")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --embed-file ../config.json@/config.json")

set_target_properties(katara PROPERTIES SUFFIX ".html")
target_include_directories(katara PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(katara webgpu dawn_internal)

# Copy artifacts
add_custom_command(TARGET katara POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/katara.js ${CMAKE_CURRENT_SOURCE_DIR}/public/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/katara.wasm ${CMAKE_CURRENT_SOURCE_DIR}/public/)
```

**Modify: `/home/charlie/code/katara/CMakeLists.txt`**
```cmake
option(BUILD_FOR_WEB "Build for Emscripten/WebAssembly" OFF)
if(BUILD_FOR_WEB)
    add_subdirectory(web)
endif()
```

### Phase 2: Main Loop Conversion

**Modify: `/home/charlie/code/katara/main.cpp`**

SDL events work in Emscripten! Only main loop changes:

```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// State struct for main loop
struct MainLoopState {
    ISimulator* simulator;
    IRenderer* renderer;
    SDL_Window* window;
    bool* running;
    int windowWidth;
    int windowHeight;
};

#ifdef __EMSCRIPTEN__
void mainLoopCallback(void* arg) {
    MainLoopState* s = static_cast<MainLoopState*>(arg);
    if (!*(s->running)) {
        emscripten_cancel_main_loop();
        return;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) *(s->running) = false;
        else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            auto coords = s->simulator->screenToGridCoords(event.button.x, event.button.y, s->windowWidth, s->windowHeight);
            if (s->simulator->isInsideCircle(coords.first, coords.second))
                s->simulator->onMouseDown(coords.first, coords.second);
        }
        else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
            s->simulator->onMouseUp();
        else if (event.type == SDL_MOUSEMOTION && event.motion.state & SDL_BUTTON_LMASK) {
            auto coords = s->simulator->screenToGridCoords(event.motion.x, event.motion.y, s->windowWidth, s->windowHeight);
            s->simulator->onMouseDrag(coords.first, coords.second);
        }
    }

    s->simulator->update();
    s->renderer->render(*s->simulator);
}
#endif

// In main(), after simulator/renderer init:
#ifdef __EMSCRIPTEN__
    MainLoopState state{simulator.get(), renderer.get(), window, &running, windowWidth, windowHeight};
    emscripten_set_main_loop_arg(mainLoopCallback, &state, 0, true);
#else
    while (running) { /* existing loop */ }
#endif
```

### Phase 3: Hand Tracking Input

**Modify: `/home/charlie/code/katara/main.cpp`**

```cpp
struct HandTrackingState {
    ISimulator* simulator = nullptr;
    int windowWidth = 0;
    int windowHeight = 0;
    bool isTracking = false;
    bool wasInside = false;
};

static HandTrackingState g_handState;

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void initHandTracking(int w, int h) {
        g_handState.windowWidth = w;
        g_handState.windowHeight = h;
    }

    EMSCRIPTEN_KEEPALIVE
    void setSimulatorPointer(void* ptr) {
        g_handState.simulator = static_cast<ISimulator*>(ptr);
    }

    EMSCRIPTEN_KEEPALIVE
    void updateHandPosition(float nx, float ny, bool present) {
        if (!g_handState.simulator || !present) {
            if (g_handState.isTracking) g_handState.simulator->onMouseUp();
            g_handState.isTracking = false;
            g_handState.wasInside = false;
            return;
        }

        int sx = static_cast<int>(nx * g_handState.windowWidth);
        int sy = static_cast<int>((1.0f - ny) * g_handState.windowHeight);
        auto coords = g_handState.simulator->screenToGridCoords(sx, sy, g_handState.windowWidth, g_handState.windowHeight);
        bool inside = g_handState.simulator->isInsideCircle(coords.first, coords.second);

        if (inside) {
            if (!g_handState.isTracking && !g_handState.wasInside) {
                g_handState.simulator->onMouseDown(coords.first, coords.second);
                g_handState.isTracking = true;
            } else if (g_handState.isTracking) {
                g_handState.simulator->onMouseDrag(coords.first, coords.second);
            }
            g_handState.wasInside = true;
        } else {
            if (g_handState.isTracking) {
                g_handState.simulator->onMouseUp();
                g_handState.isTracking = false;
            }
            g_handState.wasInside = false;
        }
    }
}
```

### Phase 4: JavaScript Integration

**New: `/home/charlie/code/katara/web/public/mediapipe.js`**
```javascript
export class MediaPipeHandTracker {
    async init(videoElement) {
        await this.loadScript('https://cdn.jsdelivr.net/npm/@mediapipe/hands/hands.js');
        this.hands = new Hands({locateFile: f => `https://cdn.jsdelivr.net/npm/@mediapipe/hands/${f}`});
        this.hands.setOptions({maxNumHands: 2, modelComplexity: 1, minDetectionConfidence: 0.5, minTrackingConfidence: 0.5});
        await this.hands.initialize();
        this.videoElement = videoElement;
    }

    async detectHands() {
        if (!this.hands) return null;
        const results = await this.hands.send({image: this.videoElement});
        if (!results.multiHandLandmarks?.length) return {x: 0, y: 0, present: false};
        const rightIdx = results.multiHandedness?.findIndex(h => h.label === 'Right') ?? 0;
        const tip = results.multiHandLandmarks[rightIdx][8];
        return {x: tip.x, y: tip.y, present: true};
    }

    loadScript(src) {
        return new Promise((r, e) => { const s = document.createElement('script'); s.src = src; s.onload = r; s.onerror = e; document.head.appendChild(s); });
    }
}
```

**New: `/home/charlie/code/katara/web/public/katara.js`**
```javascript
import { MediaPipeHandTracker } from './mediapipe.js';

class KataraWebApp {
    async init() {
        this.module = await createKataraModule();
        const canvas = document.querySelector('#canvas');
        this.module._initHandTracking(canvas.width, canvas.height);
        await this.setupCamera();
        this.handTracker = new MediaPipeHandTracker();
        await this.handTracker.init(this.videoElement);
        this.module._setSimulatorPointer(/* get from C++ or export */);
        this.processLoop();
    }

    async setupCamera() {
        this.stream = await navigator.mediaDevices.getUserMedia({video: {width: 640, height: 480}});
        this.videoElement = document.createElement('video');
        this.videoElement.srcObject = this.stream;
        this.videoElement.play();
        await new Promise(r => this.videoElement.onloadedmetadata = r);
    }

    async processLoop() {
        const pos = await this.handTracker.detectHands();
        this.module._updateHandPosition(pos.x, pos.y, pos.present);
        requestAnimationFrame(() => this.processLoop());
    }
}

document.addEventListener('DOMContentLoaded', () => new KataraWebApp().init());
```

**New: `/home/charlie/code/katara/web/public/index.html`**
```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Katara</title>
    <link rel="stylesheet" href="styles.css">
</head>
<body>
    <canvas id="canvas"></canvas>
    <script type="module" src="katara.js"></script>
</body>
</html>
```

**New: `/home/charlie/code/katara/web/public/styles.css`**
```css
* { margin: 0; padding: 0; box-sizing: border-box; }
body { background: #1a1a1a; display: flex; justify-content: center; align-items: center; min-height: 100vh; }
#canvas { max-width: 100vw; max-height: 100vh; }
```

---

## Files Summary

### Modify
| File | Changes |
|------|---------|
| `main.cpp` | Emscripten main loop, hand tracking exports |
| `CMakeLists.txt` | BUILD_FOR_WEB option, add_subdirectory(web) |

### Create
| File | Purpose |
|------|---------|
| `web/CMakeLists.txt` | Emscripten build |
| `web/public/index.html` | Canvas + scripts |
| `web/public/katara.js` | WASM loader |
| `web/public/mediapipe.js` | Hand tracking |
| `web/public/styles.css` | Styling |

### No Changes Needed
- `gpu_render.cpp` - WebGPU via SDL2WebGPU works
- `gpu_sim.cpp` - WebGPU compute works
- `irenderer.h` - Clean interface
- `isimulator.h` - `screenToGridCoords` works

---

## Build Commands

```bash
source /path/to/emsdk/emsdk_env.sh
cd /home/charlie/code/katara
mkdir -p web/build && cd web/build
emcmake cmake .. -DBUILD_FOR_WEB=ON
emmake make
cd ../public && python -m http.server 8000
# Visit http://localhost:8000
```

---

## Verification

1. Build succeeds (.js + .wasm produced)
2. WebGPU device creates (no console errors)
3. Simulation renders on canvas
4. Mouse works (SDL events functional)
5. Hand tracking controls circle

**Browser**: Chrome 113+ (WebGPU default)

---

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| WebGPU unavailable | User-friendly message |
| Shader loading fails | `--embed-file` handles; fallback to strings |
| Memory limits | `ALLOW_MEMORY_GROWTH=1` |
| Hand latency | Lower MediaPipe complexity |
| SDL_image issues | Use stb_image for PNGs |

---

## Sources

- [SDL3 Emscripten README](https://wiki.libsdl.org/SDL3/README-emscripten)
- [Learn WebGPU - Building for the Web](https://eliemichel.github.io/LearnWebGPU/appendices/building-for-the-web.html)
- [SDL Canvas Customization](https://github.com/libsdl-org/SDL/issues/5260)
- [Emscripten Canvas Sizing](https://github.com/emscripten-core/emscripten/issues/22944)
