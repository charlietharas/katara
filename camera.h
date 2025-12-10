#ifndef CAMERA_H
#define CAMERA_H

#include <SDL2/SDL.h>
#include <opencv2/opencv.hpp>
#include "config.h"

class CameraManager {
public:
    CameraManager();
    ~CameraManager();

    bool init(const CameraConfig& config);
    bool captureFrame(Uint32* pixelBuffer, int bufferWidth, int bufferHeight);
    void cleanup();

private:
    cv::VideoCapture cap;
    cv::Mat frame;
    cv::Mat resizedFrame;
    bool initialized;
    CameraConfig config;
};

#endif