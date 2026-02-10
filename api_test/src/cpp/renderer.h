#ifndef RENDERER_H
#define RENDERER_H

#include "shared_memory.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <emscripten/html5.h>
#include <emscripten/emscripten.h>

namespace handtrack {

// Hand landmark connections (for drawing lines between keypoints)
// Based on MediaPipe hand topology
static constexpr int KEYPOINT_CONNECTIONS[][2] = {
    // Thumb
    {0, 1}, {1, 2}, {2, 3}, {3, 4},
    // Index finger
    {0, 5}, {5, 6}, {6, 7}, {7, 8},
    // Middle finger
    {0, 9}, {9, 10}, {10, 11}, {11, 12},
    // Ring finger
    {0, 13}, {13, 14}, {14, 15}, {15, 16},
    // Pinky
    {0, 17}, {17, 18}, {18, 19}, {19, 20},
    // Palm
    {5, 9}, {9, 13}, {13, 17}
};
static constexpr int NUM_CONNECTIONS = sizeof(KEYPOINT_CONNECTIONS) / (2 * sizeof(int));

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Initialize renderer with canvas element
    bool init(const char* canvasSelector = "#canvas");

    // Render frame with hand keypoints overlay
    void renderFrame(const uint8_t* frameData, uint32_t width, uint32_t height,
                     const HandData* hands, size_t numHands);

    // Cleanup resources
    void cleanup();

private:
    // WebGL objects
    GLuint videoProgram;          // Shader program for video frame
    GLuint videoTexture;          // Texture for video frame
    GLuint videoVertexBuffer;     // Vertex buffer for fullscreen quad

    GLuint handProgram;           // Shader program for keypoints
    GLuint pointVertexBuffer;     // Buffer for drawing points
    GLuint lineVertexBuffer;      // Buffer for drawing connections

    // Attribute and uniform locations
    struct VideoProgramLocs {
        GLint position;
        GLint texCoord;
        GLint texture;
        GLint resolution;
    } videoLocs;

    struct HandProgramLocs {
        GLint position;
        GLint color;
        GLint pointSize;
        GLint resolution;
    } handLocs;

    // Canvas dimensions
    int canvasWidth;
    int canvasHeight;

    // Initialization helpers
    bool initVideoProgram();
    bool initHandProgram();
    bool initBuffers();

    // Shader compilation
    GLuint compileShader(GLenum type, const char* source);
    GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);

    // Drawing helpers
    void drawVideoFrame(const uint8_t* frameData, uint32_t width, uint32_t height);
    void drawHandKeypoints(const HandData* hands, size_t numHands,
                           uint32_t frameWidth, uint32_t frameHeight);
    void drawKeypoint(const float* keypoint, const float* color,
                      float pointSize, uint32_t frameWidth, uint32_t frameHeight);
    void drawConnection(const float* kp1, const float* kp2, const float* color,
                        uint32_t frameWidth, uint32_t frameHeight);

    // Color helpers
    void setHandColor(bool isLeftHand, float* color);  // Returns RGB color
};

} // namespace handtrack

#endif // RENDERER_H
