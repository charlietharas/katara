cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(katara)

# using a slightly different build process than
# https://eliemichel.github.io/LearnWebGPU/appendices/using-sdl.html
find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
find_package(OpenMP REQUIRED)

# use dawn as webgpu backend
# see webgpu/WebGPU_dawn/README.md and "/LICENSE.txt
# many thanks to Élie Michel :)
add_subdirectory(webgpu/WebGPU_dawn)
# using sdl2 with webgpu
add_subdirectory(webgpu/sdl2webgpu-main)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(katara main.cpp sim.cpp render.cpp gpu_render.cpp gpu_sim.cpp)

target_link_libraries(katara PRIVATE SDL2::SDL2 ${SDL2_IMAGE_LIBRARIES} webgpu sdl2webgpu OpenMP::OpenMP_CXX)
target_include_directories(katara PRIVATE ${SDL2_IMAGE_INCLUDE_DIRS})
target_compile_options(katara PRIVATE ${SDL2_IMAGE_CFLAGS_OTHER})

target_copy_webgpu_binaries(katara)
