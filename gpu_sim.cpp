#include "gpu_sim.h"

GPUFluidSimulator::GPUFluidSimulator(int resolution) 
    : cpuSimulator(resolution) {
}

GPUFluidSimulator::~GPUFluidSimulator() {
    // destructor TODO
}

void GPUFluidSimulator::init(const ImageData* imageData) {
    cpuSimulator.init(imageData);
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