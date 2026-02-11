cmake_minimum_required(VERSION 3.20)
project(katara_web C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# For Emscripten, create interface targets that use the built-in ports
if(EMSCRIPTEN OR DEFINED EMSCRIPTEN)
    # SDL2 via Emscripten port
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    target_compile_options(SDL2::SDL2 INTERFACE "SHELL:-s USE_SDL=2")
    target_link_options(SDL2::SDL2 INTERFACE "SHELL:-s USE_SDL=2")
    target_compile_definitions(SDL2::SDL2 INTERFACE SDL_VIDEO_DRIVER_EMSCRIPTEN)

    # SDL2_image via Emscripten port
    add_library(SDL2_image INTERFACE IMPORTED)
    target_compile_options(SDL2_image INTERFACE "SHELL:-s USE_SDL_IMAGE=2")
    target_compile_options(SDL2_image INTERFACE "SHELL:-s SDL2_IMAGE_FORMATS=[\"png\"]")
    target_link_options(SDL2_image INTERFACE "SHELL:-s USE_SDL_IMAGE=2")
    target_link_options(SDL2_image INTERFACE "SHELL:-s SDL2_IMAGE_FORMATS=[\"png\"]")

    # WebGPU via emdawnwebgpu port (provides webgpu/webgpu.h)
    add_library(webgpu INTERFACE IMPORTED)
    target_compile_options(webgpu INTERFACE "--use-port=emdawnwebgpu")
    target_link_options(webgpu INTERFACE "--use-port=emdawnwebgpu")
    target_compile_definitions(webgpu INTERFACE WEBGPU_BACKEND_EMSCRIPTEN WEBGPU_BACKEND_EMDAWNWEBGPU)

    # Add sdl2webgpu
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../webgpu/sdl2webgpu-main webgpu/sdl2webgpu-main)
endif()

# Source files
set(SOURCES
    ../main.cpp
    ../gpu_render.cpp
    ../gpu_sim.cpp
    ../render.cpp
    ../sim.cpp
    ../config.cpp
)

add_executable(katara ${SOURCES})

# Link libraries (SDL2::SDL2 includes SDL2 port, SDL2_image includes SDL2_image port)
target_link_libraries(katara PRIVATE SDL2::SDL2 SDL2_image webgpu sdl2webgpu)

# Suppress deprecated literal operator warnings from json.hpp
target_compile_options(katara PRIVATE -Wno-deprecated-literal-operator)

# Emscripten linker flags
target_link_options(katara PUBLIC
    "SHELL:-s WASM=1"
    "--use-port=emdawnwebgpu"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s MAXIMUM_MEMORY=4GB"
    "SHELL:-s ASYNCIFY"
    "SHELL:-s EXPORTED_FUNCTIONS=[\"_main\",\"_initHandTracking\",\"_updateHandPosition\",\"_setSimulatorPointer\"]"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\"]"
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_NAME=\"createKataraModule\""
)

# Stage shader and config files for embedding
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(EMBED_STAGE_DIR ${CMAKE_CURRENT_BINARY_DIR}/embed)
file(MAKE_DIRECTORY ${EMBED_STAGE_DIR})
file(GLOB SHADER_FILES "${PROJECT_ROOT}/*.wgsl")
foreach(SHADER ${SHADER_FILES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    configure_file(${SHADER} "${EMBED_STAGE_DIR}/${SHADER_NAME}" COPYONLY)
endforeach()
configure_file("${PROJECT_ROOT}/config.json" "${EMBED_STAGE_DIR}/config.json" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/public/img1.png" "${EMBED_STAGE_DIR}/img1.png" COPYONLY)

# Embed data files (mounted at / in Emscripten virtual FS)
target_link_options(katara PUBLIC
    "SHELL:--embed-file ${EMBED_STAGE_DIR}@/"
)

# Include directories
target_include_directories(katara PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Output as .js + .wasm (we provide our own HTML shell in public/)
set_target_properties(katara PROPERTIES SUFFIX ".js")

# Copy artifacts to public/
add_custom_command(TARGET katara POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/katara.js ${CMAKE_CURRENT_SOURCE_DIR}/public/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/katara.wasm ${CMAKE_CURRENT_SOURCE_DIR}/public/)
