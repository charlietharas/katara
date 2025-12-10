#include "camera.h"
#include <iostream>

CameraManager::CameraManager()
    : initialized(false) {
}

CameraManager::~CameraManager() {
    cleanup();
}

bool CameraManager::init(const CameraConfig& cameraConfig) {
    config = cameraConfig;

    cap.open(config.deviceID, cv::CAP_V4L2);
    if (!cap.isOpened() || !cap.read(frame) || frame.empty()) {
        std::cout << "Failed to open camera device " << config.deviceID << std::endl;
        return false;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, config.width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, config.height);
    cap.set(cv::CAP_PROP_FPS, config.framerate);

    initialized = true;
    return true;
}

bool CameraManager::captureFrame(Uint32* pixelBuffer, int bufferWidth, int bufferHeight) {
    if (!initialized || !cap.isOpened()) { return false; }

    // capture frame
    if (!cap.read(frame)) {
        return false;
    }

    // resize to buffer dimensions
    cv::resize(frame, resizedFrame, cv::Size(bufferWidth, bufferHeight));

    // ARGB
    for (int y = 0; y < bufferHeight; y++) {
        for (int x = 0; x < bufferWidth; x++) {
            cv::Vec3b pixel = resizedFrame.at<cv::Vec3b>(y, x);
            Uint8 r = pixel[2];
            Uint8 g = pixel[1];
            Uint8 b = pixel[0];
            pixelBuffer[y * bufferWidth + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }

    return true;
}

void CameraManager::cleanup() {
    if (cap.isOpened()) {
        cap.release();
    }
    frame.release();
    resizedFrame.release();
    initialized = false;
}