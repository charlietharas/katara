#include "gpu_sim.h"

GPUFluidSimulator::GPUFluidSimulator(const Config& config)
    : cpuSimulator(config) {
}

GPUFluidSimulator::~GPUFluidSimulator() {
}

void GPUFluidSimulator::init(const Config& config, const ImageData* imageData) {
    cpuSimulator.init(config, imageData);
}

void GPUFluidSimulator::update() {
    // TODO replace with compute shader dispatches
    cpuSimulator.update();
}

void GPUFluidSimulator::onMouseDown(int gridX, int gridY) {
    cpuSimulator.onMouseDown(gridX, gridY);
}

void GPUFluidSimulator::onMouseDrag(int gridX, int gridY) {
    cpuSimulator.onMouseDrag(gridX, gridY);
}

void GPUFluidSimulator::onMouseUp() {
    cpuSimulator.onMouseUp();
}