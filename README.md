# Katara

## Build Instructions (Linux)
Dependencies:

- See [installation instructions for the Dawn backend for WebGPU](https://eliemichel.github.io/LearnWebGPU/getting-started/hello-webgpu.html#option-b-the-comfort-of-dawn)
  - While the backend is included in `webgpu/WebGPU_dawn`, you will need to install dependencies specified on the website to get it to build
- You will also need SDL2 (`libsdl2-dev`)
  - This project modifies the installation process specified [here](https://eliemichel.github.io/LearnWebGPU/appendices/using-sdl.html), changing some of the header files to work with libsdl 2.0.20

To build:

```
cmake -B build
cd build
make
```

To run:
```
./katara
```

## Usage
Edit `config.json` to change simulation behavior. Command line arguments are not supported.

**TODO config description**

## Project Structure
`main.cpp` and `config.cpp` manage program initialization and main loop. Simulation parameters are loaded from `config.json`.

Simulation has two components, which are fully implemented on both the CPU and GPU (via WebGPU). Use the configuration file to switch between host/device rendering and simulation (pipeline="host","device","hybrid"; GPU simulation with CPU rendering is unsupported).

**Renderer** (abstract interface defined in `irenderer.h`)
- CPU version in `render.cpp`
- GPU version in `gpu_render.cpp`; shaders in `fragment.wgsl` and `vertex.wgsl`

**Simulator** (abstract interface defined in `isimulator.h`)
- CPU version in `sim.cpp`
- GPU version in `gpu_sim.cpp`