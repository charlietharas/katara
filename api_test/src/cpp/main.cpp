#include <emscripten.h>
#include <emscripten/html5.h>
#include "renderer.h"
#include "hand_tracker.h"
#include "shared_memory.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace handtrack {

// Global objects
Renderer* g_renderer = nullptr;
HandTracker* g_handTracker = nullptr;

// Frame data buffers (allocated once, reused)
std::vector<uint8_t> g_frameBuffer;
std::vector<HandData> g_handDataBuffer;

} // namespace handtrack

using namespace handtrack;

// Message handler from JavaScript
extern "C" {

// Structure for incoming message data
struct FrameMessage {
    uint32_t width;
    uint32_t height;
    uint32_t numHands;
    // Followed by frame data (RGBA) and hand data
};

// Initialize the WASM module
EMSCRIPTEN_KEEPALIVE
int init(const char* canvasSelector) {
    if (g_renderer || g_handTracker) {
        // Already initialized
        return 0;
    }

    g_renderer = new Renderer();
    g_handTracker = new HandTracker();

    // Initialize renderer with canvas
    if (!g_renderer->init(canvasSelector ? canvasSelector : "#canvas")) {
        fprintf(stderr, "Failed to initialize renderer\n");
        return -1;
    }

    fprintf(stderr, "WASM module initialized\n");
    return 0;
}

// Process frame data sent from JavaScript
EMSCRIPTEN_KEEPALIVE
void processFrame(uint8_t* frameData, uint32_t width, uint32_t height,
                  void* handData, uint32_t numHands) {
    if (!g_renderer || !g_handTracker) {
        fprintf(stderr, "Module not initialized\n");
        return;
    }

    // Resize frame buffer if needed
    size_t frameSize = width * height * 4;  // RGBA
    if (g_frameBuffer.size() < frameSize) {
        g_frameBuffer.resize(frameSize);
    }

    // Copy frame data
    memcpy(g_frameBuffer.data(), frameData, frameSize);

    // Copy hand data
    g_handDataBuffer.clear();
    if (numHands > 0 && handData) {
        HandData* hands = static_cast<HandData*>(handData);
        for (uint32_t i = 0; i < numHands && i < 2; i++) {
            if (hands[i].confidence > 0.0f) {
                g_handDataBuffer.push_back(hands[i]);
            }
        }
    }

    // Process hands
    g_handTracker->processHands(g_handDataBuffer.data(), g_handDataBuffer.size());

    // Render frame with keypoints overlay
    g_renderer->renderFrame(g_frameBuffer.data(), width, height,
                           g_handDataBuffer.data(), g_handDataBuffer.size());
}

// Alternative: Process frame from JavaScript using direct pointer
EMSCRIPTEN_KEEPALIVE
void processFramePtr(uint32_t framePtr, uint32_t width, uint32_t height,
                     uint32_t handDataPtr, uint32_t numHands) {
    uint8_t* frameData = reinterpret_cast<uint8_t*>(framePtr);
    HandData* handData = reinterpret_cast<HandData*>(handDataPtr);
    processFrame(frameData, width, height, handData, numHands);
}

// Cleanup
EMSCRIPTEN_KEEPALIVE
void cleanup() {
    if (g_renderer) {
        g_renderer->cleanup();
        delete g_renderer;
        g_renderer = nullptr;
    }

    if (g_handTracker) {
        delete g_handTracker;
        g_handTracker = nullptr;
    }

    g_frameBuffer.clear();
    g_handDataBuffer.clear();

    fprintf(stderr, "WASM module cleaned up\n");
}

} // extern "C"

// Main entry point (not used in browser context)
int main(int argc, char* argv[]) {
    fprintf(stderr, "Hand Tracking WASM Module loaded\n");
    return 0;
}
