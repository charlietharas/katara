# Katara

## Usage

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