#ifndef LAYOUT_H
#define LAYOUT_H

#include "config.h"
#include <string>

std::string layoutPixelsToJson(const LayoutPixels& pixels, const LayoutConfig& config);

void applyDefaultComponentLayout(const LayoutConfig& config, int canvasW, int canvasH);

void applyLayoutPreset(const LayoutConfig& config,
                       int canvasW,
                       int canvasH,
                       float viewportAspectRatio,
                       float cameraAspectRatio,
                       bool isInkMode);

#endif
