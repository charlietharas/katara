#include "gpu_sim.h"

GPUFluidSimulator::GPUFluidSimulator() {
    // constructor TODO
}

GPUFluidSimulator::~GPUFluidSimulator() {
    // destructor TODO
}

void GPUFluidSimulator::init(bool imageLoaded) {
    cpuSimulator.init(imageLoaded);
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