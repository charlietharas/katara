cmake_minimum_required(VERSION 3.20)
project(katara)

# using a slightly different build process than
# https://eliemichel.github.io/LearnWebGPU/appendices/using-sdl.html
find_package(SDL2 REQUIRED)

# use dawn as webgpu backend
# see webgpu/WebGPU_dawn/README.md and "/LICENSE.txt
# many thanks to Élie Michel :)
add_subdirectory(webgpu/WebGPU_dawn)
# using sdl2 with webgpu
add_subdirectory(webgpu/sdl2webgpu-main)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(katara main.cpp sim.cpp render.cpp)

target_link_libraries(katara PRIVATE SDL2::SDL2 webgpu sdl2webgpu)

target_copy_webgpu_binaries(katara)
