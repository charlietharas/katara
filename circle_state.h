#pragma once
#include <cstdint>

namespace HandTracking {
    static constexpr int MAX_CIRCLES = 42; // 21 landmarks * 2 hands
    static constexpr int LANDMARKS_PER_HAND = 21;
    
    // hand skeleton connections (MediaPipe topology)
    static constexpr int HAND_CONNECTIONS[][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 4}, // thumb
        {0, 5}, {5, 6}, {6, 7}, {7, 8}, // index
        {0, 9}, {9, 10}, {10, 11}, {11, 12}, // middle
        {0, 13}, {13, 14}, {14, 15}, {15, 16}, // ring
        {0, 17}, {17, 18}, {18, 19}, {19, 20}, // pinky
        {5, 9}, {9, 13}, {13, 17} // palm webbing
    };
    static constexpr int MAX_CONNECTIONS = sizeof(HAND_CONNECTIONS) / sizeof(HAND_CONNECTIONS[0]);
    static constexpr int MAX_SEGMENTS = MAX_CONNECTIONS * 2; // 2 hands
}

struct CircleState {
    int x, y;
    int prevX, prevY;
    float velX, velY;
    float z;
    int scaledRadius;
    bool present;
    bool wasPresent;
};

struct FingertipData {
    float x, y, z;
    float present;
};

struct LineSegment {
    int startX, startY;
    int endX, endY;
    int prevStartX, prevStartY;
    int prevEndX, prevEndY;
    float startRadius;
    float endRadius;
    float prevStartRadius;
    float prevEndRadius;
    bool present;
    bool wasPresent;
};
