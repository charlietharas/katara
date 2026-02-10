#include "renderer.h"
#include <cstdio>
#include <cstring>

namespace handtrack {

// Vertex shader for rendering video frame
static const char* videoVertexShader = R"(
    attribute vec2 a_position;
    attribute vec2 a_texCoord;
    varying vec2 v_texCoord;
    void main() {
        gl_Position = vec4(a_position, 0.0, 1.0);
        v_texCoord = a_texCoord;
    }
)";

// Fragment shader for rendering video frame
static const char* videoFragmentShader = R"(
    precision mediump float;
    varying vec2 v_texCoord;
    uniform sampler2D u_texture;
    void main() {
        gl_FragColor = texture2D(u_texture, v_texCoord);
    }
)";

// Vertex shader for rendering hand keypoints (simplified)
static const char* handVertexShader = R"(
    attribute vec2 a_position;
    uniform vec3 u_color;
    void main() {
        // a_position is already in clip space (-1 to 1)
        gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0);
        gl_PointSize = 20.0;
    }
)";

// Fragment shader for rendering hand keypoints (simplified - solid squares)
static const char* handFragmentShader = R"(
    precision mediump float;
    uniform vec3 u_color;
    void main() {
        gl_FragColor = vec4(u_color, 1.0);
    }
)";

// Fullscreen quad vertices (position, texCoord)
static const float quadVertices[] = {
    // Position  (x, y)    TexCoord  (u, v)
    -1.0f, -1.0f,           0.0f, 1.0f,  // Bottom-left
     1.0f, -1.0f,           1.0f, 1.0f,  // Bottom-right
    -1.0f,  1.0f,           0.0f, 0.0f,  // Top-left
     1.0f,  1.0f,           1.0f, 0.0f   // Top-right
};

Renderer::Renderer()
    : videoProgram(0)
    , videoTexture(0)
    , videoVertexBuffer(0)
    , handProgram(0)
    , pointVertexBuffer(0)
    , lineVertexBuffer(0)
    , canvasWidth(0)
    , canvasHeight(0)
{
}

Renderer::~Renderer() {
    cleanup();
}

bool Renderer::init(const char* canvasSelector) {
    // Get canvas dimensions
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.depth = false;
    attrs.stencil = false;
    attrs.antialias = true;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context(canvasSelector + 1, &attrs);
    if (ctx == 0) {
        fprintf(stderr, "Failed to create WebGL context\n");
        return false;
    }

    emscripten_webgl_make_context_current(ctx);

    // Get canvas size
    emscripten_get_canvas_element_size(canvasSelector + 1, &canvasWidth, &canvasHeight);

    glViewport(0, 0, canvasWidth, canvasHeight);

    // Initialize shaders and buffers
    if (!initVideoProgram()) {
        fprintf(stderr, "Failed to initialize video program\n");
        return false;
    }

    if (!initHandProgram()) {
        fprintf(stderr, "Failed to initialize hand program\n");
        return false;
    }

    if (!initBuffers()) {
        fprintf(stderr, "Failed to initialize buffers\n");
        return false;
    }

    // Set OpenGL state
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    return true;
}

bool Renderer::initVideoProgram() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, videoVertexShader);
    if (vs == 0) return false;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, videoFragmentShader);
    if (fs == 0) {
        glDeleteShader(vs);
        return false;
    }

    videoProgram = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (videoProgram == 0) return false;

    // Get attribute and uniform locations
    videoLocs.position = glGetAttribLocation(videoProgram, "a_position");
    videoLocs.texCoord = glGetAttribLocation(videoProgram, "a_texCoord");
    videoLocs.texture = glGetUniformLocation(videoProgram, "u_texture");
    videoLocs.resolution = glGetUniformLocation(videoProgram, "u_resolution");

    return true;
}

bool Renderer::initHandProgram() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, handVertexShader);
    if (vs == 0) return false;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, handFragmentShader);
    if (fs == 0) {
        glDeleteShader(vs);
        return false;
    }

    handProgram = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (handProgram == 0) return false;

    // Get attribute and uniform locations
    handLocs.position = glGetAttribLocation(handProgram, "a_position");
    handLocs.color = glGetUniformLocation(handProgram, "u_color");
    handLocs.pointSize = -1;  // Not used anymore (fixed in shader)
    handLocs.resolution = -1;  // Not used anymore

    return true;
}

bool Renderer::initBuffers() {
    // Create vertex buffer for fullscreen quad
    glGenBuffers(1, &videoVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, videoVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Create texture for video frame
    glGenTextures(1, &videoTexture);
    glBindTexture(GL_TEXTURE_2D, videoTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create vertex buffer for hand drawing
    glGenBuffers(1, &pointVertexBuffer);
    glGenBuffers(1, &lineVertexBuffer);

    return true;
}

void Renderer::renderFrame(const uint8_t* frameData, uint32_t width, uint32_t height,
                           const HandData* hands, size_t numHands) {
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw video frame
    drawVideoFrame(frameData, width, height);

    // Draw hand keypoints
    if (numHands > 0) {
        drawHandKeypoints(hands, numHands, width, height);
    }

    // Present
    emscripten_webgl_commit_frame();
}

void Renderer::drawVideoFrame(const uint8_t* frameData, uint32_t width, uint32_t height) {
    glUseProgram(videoProgram);

    // Update texture with new frame data
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, videoTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frameData);

    // Set uniforms
    glUniform1i(videoLocs.texture, 0);
    glUniform2f(videoLocs.resolution, (float)canvasWidth, (float)canvasHeight);

    // Bind vertex buffer
    glBindBuffer(GL_ARRAY_BUFFER, videoVertexBuffer);
    glEnableVertexAttribArray(videoLocs.position);
    glVertexAttribPointer(videoLocs.position, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(videoLocs.texCoord);
    glVertexAttribPointer(videoLocs.texCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    // Draw quad
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Cleanup
    glDisableVertexAttribArray(videoLocs.position);
    glDisableVertexAttribArray(videoLocs.texCoord);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::drawHandKeypoints(const HandData* hands, size_t numHands,
                                 uint32_t frameWidth, uint32_t frameHeight) {
    glUseProgram(handProgram);

    // Draw each hand
    for (size_t handIdx = 0; handIdx < numHands; handIdx++) {
        if (hands[handIdx].confidence <= 0.0f) continue;

        const HandData& hand = hands[handIdx];
        float handColor[3];
        setHandColor(hand.handedness == 0, handColor);
        glUniform3fv(handLocs.color, 1, handColor);

        // Draw each keypoint as a small square (using triangle strip)
        const float keySize = 0.025f;  // Size in clip space (larger)
        for (int i = 0; i < 21; i++) {
            float nx = hand.keypoints[i][0] * 2.0f - 1.0f;
            float ny = 1.0f - hand.keypoints[i][1] * 2.0f;  // Flip Y

            // Create a small quad (2 triangles) for each keypoint
            float square[] = {
                nx - keySize, ny - keySize,
                nx + keySize, ny - keySize,
                nx - keySize, ny + keySize,
                nx + keySize, ny + keySize,
            };

            glBindBuffer(GL_ARRAY_BUFFER, pointVertexBuffer);
            glBufferData(GL_ARRAY_BUFFER, sizeof(square), square, GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(handLocs.position);
            glVertexAttribPointer(handLocs.position, 2, GL_FLOAT, GL_FALSE, 0, 0);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glDisableVertexAttribArray(handLocs.position);
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void Renderer::drawKeypoint(const float* keypoint, const float* color,
                            float pointSize, uint32_t frameWidth, uint32_t frameHeight) {
    // Convert normalized (0-1) to clip space (-1 to 1)
    float x = keypoint[0] * 2.0f - 1.0f;
    float y = 1.0f - keypoint[1] * 2.0f;  // Flip Y for WebGL

    glUniform3fv(handLocs.color, 1, color);

    // Unbind any buffer so immediate mode works
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(handLocs.position);
    glVertexAttrib2f(handLocs.position, x, y);
    glDrawArrays(GL_POINTS, 0, 1);
    glDisableVertexAttribArray(handLocs.position);
}

void Renderer::drawConnection(const float* kp1, const float* kp2, const float* color,
                              uint32_t frameWidth, uint32_t frameHeight) {
    // Convert normalized (0-1) to clip space (-1 to 1)
    float x1 = kp1[0] * 2.0f - 1.0f;
    float y1 = 1.0f - kp1[1] * 2.0f;  // Flip Y for WebGL
    float x2 = kp2[0] * 2.0f - 1.0f;
    float y2 = 1.0f - kp2[1] * 2.0f;  // Flip Y for WebGL

    float vertices[] = { x1, y1, x2, y2 };

    glUniform3fv(handLocs.color, 1, color);

    glBindBuffer(GL_ARRAY_BUFFER, lineVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(handLocs.position);
    glVertexAttribPointer(handLocs.position, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_LINES, 0, 2);
    glDisableVertexAttribArray(handLocs.position);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Renderer::setHandColor(bool isLeftHand, float* color) {
    // Left hand: green, Right hand: orange
    if (isLeftHand) {
        color[0] = 0.0f;  // R
        color[1] = 1.0f;  // G
        color[2] = 0.5f;  // B
    } else {
        color[0] = 1.0f;  // R
        color[1] = 0.6f;  // G
        color[2] = 0.0f;  // B
    }
}

void Renderer::cleanup() {
    if (videoProgram) glDeleteProgram(videoProgram);
    if (handProgram) glDeleteProgram(handProgram);
    if (videoTexture) glDeleteTextures(1, &videoTexture);
    if (videoVertexBuffer) glDeleteBuffers(1, &videoVertexBuffer);
    if (pointVertexBuffer) glDeleteBuffers(1, &pointVertexBuffer);
    if (lineVertexBuffer) glDeleteBuffers(1, &lineVertexBuffer);
}

GLuint Renderer::compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        fprintf(stderr, "Shader compilation error: %s\n", infoLog);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint Renderer::linkProgram(GLuint vertexShader, GLuint fragmentShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        fprintf(stderr, "Program linking error: %s\n", infoLog);
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

} // namespace handtrack
