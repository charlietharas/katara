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

Parameters will either change fluid behavior, simulation components (e.g. fluid inlet or interactive circle obstacle), or visualizations. Notably, changing `target=3` and setting `imagePath` will load an image and distort it with fluid dynamics ("ink mode").

| Section        | Parameter                 | Description                                                                                                             |
| -------------- | ------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| pipeline       |                           | `host`=run all calculations on CPU, `device`=run all calculations on GPU, `hybrid`=run simulation on CPU, render with GPU     |
| window         | baseSize                  | Size of larger window dimension when images are loaded in ink mode (px)                                                |
|                | defaultWidth              | Default window width in non-ink mode (px)                                                                               |
|                | defaultHeight             | Default window height in non-ink mode (px)                                                                              |
| simulation     | resolution                | Number of grid cells along the smaller dimension of the window                                                          |
|                | timestep                  | Timescale resolution of the simulation; recommend leaving at 1/60                                                       |
|                | gravity                   | Apply vertical acceleration to grid in integration step                                                       |
|                | fluidDensity              | Affects fluid pressure and general behavior; recommend leaving at 1000                                                  |
| sim.projection | overrelaxationCoefficient | Used to speed up pressure projection; recommend leaving at 1.9                                                          |
|                | iterations                | Number of pressure solver iterations to run; recommend increasing when running on device |
| sim.vorticity  | enabled                   | Apply pretty swirling patterns to fluid in simulation; recommend leaving enabled                                        |
|                | strength                  | Vorticity strength                                                                                                      |
|                | lengthScale               | Length of vorticity effect                                                                                              |
| sim.windTunnel | side                      | Side of fluid inlet; `0`=left, `1`=top, `2`=bottom, `3`=right, `-1`=disabled                                                      |
|                | startPosition             | Beginning of fluid inlet along side in normalized world coords (0-1)                                                               |
|                | endPosition               | End of fluid inlet along side in normalized world coords (0-1)                                                                     |
|                | velocity                  | Velocity of fluid coming in from the inlet                                                                              |
| sim.circle     | radius                    | Radius of interactive circle obstacle; 0 to disable                                                                     |
|                | momentumTransferCoeff     | Strength with which circle imparts velocity on nearby fluid when moved                                                  |
|                | momentumTransferRadius    | Distance around the circle center around which velocity is imparted (with quadratic falloff)                              |
| rendering      | target                    | `0`=colored density grid, `1`=greyscale smoke grid, `2`=combined smoke/density grid, `3`=ink mode (requires `imagePath` set)             |
|                | showVelocityVectors       | Enable/disable rendering velocity grid as white lines                                                                   |
|                | velocityScale             | Controls the length of displayed velocity vectors                                                                       |
|                | disableHistograms         | Disable calculating and rendering density and velocity histograms                                                       |
| ink            | imagePath                 | Path (relative to executable) to load input image for ink mode                                                          |
## Project Structure
`main.cpp` and `config.cpp` manage program initialization and main loop. Simulation parameters are loaded from `config.json`.

Simulation has two components, which are fully implemented on both the CPU and GPU (via WebGPU). Use the configuration file to switch between host/device/hybrid rendering and simulation (GPU simulation with CPU rendering is unsupported).

**Renderer** (abstract interface defined in `irenderer.h`)
- CPU version in `render.cpp`
- GPU version in `gpu_render.cpp`; shaders in `fragment.wgsl` and `vertex.wgsl`

**Simulator** (abstract interface defined in `isimulator.h`)
- CPU version in `sim.cpp`
- GPU version in `gpu_sim.cpp`; each step of the main simulation loop has its own compute shader file `compute_<stage>.wgsl`