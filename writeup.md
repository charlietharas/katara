# Setup

## General structure

## Entrypoint

Entrypoint is `main.cpp`
- notice immediately we have two build options for web/desktop depending on how event handlers, main loop need to be set up; there is naturally boilerplate to bridge to web
- main: initialize simulator/renderer using values from config, load image data, expose functions to js, dispatch initial layout computations, run main sim/render loop and handle events

`config`
- JSON-based, primary function is to get file -> object params

`image loading`
- affects aspect ratio for layout calculations
- loads image as texture to be passed forward to shaders

# Computation

## GPU

### Pipeline
- init
- loop logistics/setup (uniform buffer, submission)
- line segments
- user object input
- projection: divergence -> jacobi -> jacobi_apply
- extrapolate
- advect (velocity, density, ink)
- vorticity: curl -> vorticity
- boundary conditions

## Actual Config Params

# Rendering

## Layout computation
- reusable, done on window resize
- split grid into 2x2 quadrants and an optional bottom pane
- calculate within each quadrant: either viewport fills, camera + plot handling, or just plot handling
- updates from config and returns pixel layouts used by dom to render buttons/labels using absolute values; this is the render bridge between js/cpp