# Katara

Modular fluid simulation. Inspired by a [lovely video](https://www.youtube.com/watch?v=iKAVRgIrUOU) by 10 Minute Physics.

## TODO update readme after build options, fixes complete

Currently still dotting the i's and crossing the t's on code tidying, clean build options, etc.

But will soon support Emscripten builds so that we can host the WASM-compiled binaries on GitHub pages and deploy to web!

## Build Instructions (Linux)
Dependencies:

- See [installation instructions for the Dawn backend for WebGPU](https://eliemichel.github.io/LearnWebGPU/getting-started/hello-webgpu.html#option-b-the-comfort-of-dawn)
  - While the backend is included in `webgpu/WebGPU_dawn`, you will need to install dependencies specified on the website to get it to build
- You will also need SDL2 (`libsdl2-dev`)
  - This project modifies the installation process specified [here](https://eliemichel.github.io/LearnWebGPU/appendices/using-sdl.html), changing some of the header files to work with libsdl 2.0.20

To build:

Release:
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cd build
make
```

Debug:
```
cmake -B debug -DCMAKE_BUILD_TYPE=Debug
cd debug
make
```

## Usage
To run:
```
./katara
# optionally
./katara ./path/to/config.json
```

## Scripts
By default, the simulator prints min/max pressure values per tick. A script is provided to visualize these:
```
./katara | python3 ../plot_pressure.py
```

I will update `results/` with interesting findings, and expect to use this (and further) scripts to debug the simulation.

## Parameters
Edit `config.json` to change simulation behavior. Switch between config files with `./katara file.json`. Command line args for individual config options currently not suported.

Parameters will either change fluid behavior, simulation components (e.g. fluid inlet or interactive "circle" obstacle), or visualizations. Notably, setting `target=3` will cause the program to load a PNG image from `imagePath` and distort it with fluid dynamics ("ink mode").

| Section        | Parameter                 | Description                                                                                                             |
| -------------- | ------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| pipeline       |                           | `host`=run all calculations on CPU, `device`=run all calculations on GPU, `hybrid`=run simulation on CPU, render with GPU     |
| window         | baseSize                  | Size of smaller window dimension when images are loaded in ink mode (px)                                                |
|                | defaultWidth              | Default window width in non-ink mode (px)                                                                               |
|                | defaultHeight             | Default window height in non-ink mode (px)                                                                              |
| simulation     | resolution                | Number of grid cells along the smaller dimension of the window                                                          |
|                | timestep                  | Timescale resolution (each simulation tick)                                                     |
|                | gravity                   | Apply vertical acceleration to grid (*known bugs with* `edges!=15`)                                                       |
|                | fluidDensity              | Affects fluid dynamics                                                  |
|| edges | Decimal 0-15 interpreted as bitstring (`left-top-bottom-right`) | |
| sim.projection | overrelaxationCoefficient | Used to speed up CPU pressure solving; recommend leaving at 1.9. GPU simulator ignores this as it uses a different pressure solver                                                          |
|                | iterations                | Number of pressure solver iterations to run. GPU solver requires drastically more iterations than CPU (I observed similar behavior at 1000/40) |
| sim.vorticity  | enabled                   | Apply pretty swirling patterns to fluid in simulation; recommend leaving enabled                                        |
|                | strength                  | Vorticity strength (scaling factor)                                                                                                      |
|                | lengthScale               | Length of vorticity effect (grid units)                                                                                             |
| sim.windTunnel | side                      | Side of fluid inlet; `0`=left, `1`=top, `2`=bottom, `3`=right, `-1`=disabled                                                      |
|                | startPosition             | Beginning of fluid inlet along side in normalized world coords (0-1)                                                               |
|                | endPosition               | End of fluid inlet along side in normalized world coords (0-1)                                                                     |
|                | velocity                  | Velocity of fluid coming in from the inlet (grid units/s)                                                                             |
| sim.circle     | radius                    | Radius of interactive circle obstacle in normalized world coords (0-1); 0 to disable                                                 |
|                | momentumTransferStrength     | Strength with which circle imparts velocity on nearby fluid when moved (scaling factor)                                                 |
|                | momentumTransferRadius    | Distance around the circle center around which velocity is imparted (with quadratic falloff); proportional to radius                              |
| sim.circle.zScaling | zMin                    | Hand z-coordinate treated as closest (depth); used for radius scaling in hand-tracking mode                                                 |
|                | zMax                    | Hand z-coordinate treated as farthest; used for radius scaling in hand-tracking mode                                                 |
|                | scaleMin                | Radius scale factor when hand is closest (z=zMin); larger = bigger circle when near camera                                                 |
|                | scaleMax                | Radius scale factor when hand is farthest (z=zMax); smaller = smaller circle when far from camera                                                 |
| rendering      | target                    | `0`=colored density grid, `1`=greyscale smoke grid, `2`=combined smoke/density grid, `3`=ink mode (requires `imagePath` set)             |
|                | showVelocityVectors       | Enable/disable rendering velocity grid as white lines                                                                   |
|                | velocityScale             | Controls the length of displayed velocity vectors                                                                       |
|                | disableHistograms         | Disable calculating and rendering density and velocity histograms                                                       |
| ink            | imagePath                 | Path to load input image for ink mode                                                          |
## Project Structure
`main.cpp` and `config.cpp` manage program initialization and main loop. Simulation parameters are loaded from `config.json`.

Main loop has two components, which are fully implemented on both the CPU and GPU (via WebGPU). Use a config file to switch between configurations.

**Renderer** (abstract interface defined in `irenderer.h`)
- CPU version in `render.cpp`
- GPU version in `gpu_render.cpp`; shaders in `fragment.wgsl` (for HYBRID mode), `fragment_gpu.wgsl` (for GPU mode), and `vertex.wgsl`

**Simulator** (abstract interface defined in `isimulator.h`)
- CPU version in `sim.cpp`
- GPU version in `gpu_sim.cpp`; each step of the main simulation loop has its own compute shader file `compute_<stage>.wgsl`.
  - While I tried to avoid idiosyncracies, note that histogram computation takes place using buffered asynchronous callbacks for GPU mode, but is calculated synchronously in the renderer for CPU/HYBRID

**Boilerplate**:
- Macros and helper functions are defined in `boilerplate.h` for reuse between the GPU simulator and renderer
- `json.hpp` is ostensibly a helper for config loading

## Project Logic
In the main loop, the simulator executes the fluid simulation pipeline for one tick, then the renderer draws the current frame.

### Simulation
1. **Integration**: apply constant vertical velocity to field
2. **Projection**: pressure solving (by far the most computationally costly stage of the pipeline)
   - CPU uses Gauss-Seidel pressure solving with overrelaxation
   - GPU uses Jacobi pressure solving; requires additional passes to precompute divergence and apply pressure application
3. **Extrapolation**: preserve simulation bounds by copying interior velocities to border
4. **Advection**: semi-Lagrangian advection with bilinear interpolation; backtracks along velocity field
   - GPU needs three passes for the different textures
5. **Vorticity**: applies force along vorticity gradient to intensify vortices (really to make it prettier)
6. **Circle and boundary handling**
   - CPU uses inline helpers that are called dynamically when the circle moves
   - GPU has two compute passes each frame to handle boundary conditions, circle drawing, momentum transfer, and velocity clearing
7. **Histogram handling**
   - CPU/HYBRID modes compute histograms in renderer synchronously
   - GPU uses atomic operations to compute values in the simulator with async callbacks written to ring buffer

### Renderer
1. Grab simulation data (either from CPU field vectors or GPU field textures)
2. Calculate histograms (sometimes, see above)
3. Render fluid fields
4. Render velocity vectors (if enabled)
5. Render pressure and velocity histograms
