cmake_minimum_required(VERSION 3.20)

project(katara_web C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# mouse input
# OFF = hand tracking
# ON = single circle mouse control
option(ENABLE_MOUSE_INPUT "Enable mouse input mode (OFF=hand tracking)" OFF)

# I trust Claude here ngl
# for Emscripten, create interface targets that use the built-in ports
# the EMSCRIPTEN variable is set by the Emscripten toolchain after project()
if(EMSCRIPTEN)
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

    # add sdl2webgpu
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../webgpu/sdl2webgpu-main webgpu/sdl2webgpu-main)
endif()

set(SOURCES
    ../main.cpp
    ../gpu_render.cpp
    ../gpu_sim.cpp
    ../render.cpp
    ../sim.cpp
    ../config.cpp
)

add_executable(katara ${SOURCES})

if(ENABLE_MOUSE_INPUT)
    target_compile_definitions(katara PRIVATE ENABLE_MOUSE_INPUT)
    target_compile_options(katara PRIVATE "SHELL:-D ENABLE_MOUSE_INPUT")
endif()

target_link_libraries(katara PRIVATE SDL2::SDL2 SDL2_image webgpu sdl2webgpu)

# suppress deprecated literal operator warnings from json.hpp
target_compile_options(katara PRIVATE -Wno-deprecated-literal-operator)

# emscripten linker flags
if(ENABLE_MOUSE_INPUT)
    set(EXPORTED_FUNCTIONS "\"_main\",\"_malloc\",\"_free\"")
else()
    # hand tracking mode needs to make extra functions available to web app
    set(EXPORTED_FUNCTIONS "\"_main\",\"_malloc\",\"_free\",\"_setSimulatorPointer\",\"_updateFingertips\",\"_updateLineSegments\",\"_update\"")
endif()

target_link_options(katara PUBLIC
    "SHELL:-s WASM=1"
    "--use-port=emdawnwebgpu"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s MAXIMUM_MEMORY=4GB"
    "SHELL:-s ASYNCIFY"
    "SHELL:-s EXPORTED_FUNCTIONS=[${EXPORTED_FUNCTIONS}]"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\",\"HEAPF32\",\"FS\",\"IDBFS\",\"callMain\"]"
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_NAME=\"createKataraModule\""
    "SHELL:-s FORCE_FILESYSTEM=1"
    "-lidbfs.js"
)

# stage shader and config files for embedding
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(EMBED_STAGE_DIR ${CMAKE_CURRENT_BINARY_DIR}/embed)
file(MAKE_DIRECTORY ${EMBED_STAGE_DIR})
file(GLOB SHADER_FILES "${PROJECT_ROOT}/*.wgsl")

# compute_circle has two WGSL variants; pick based on ENABLE_MOUSE_INPUT
# essentially we inject the correct variant as compute_circle.wgsl so gpu_sim.cpp can pick it up at runtime
if(ENABLE_MOUSE_INPUT)
    set(COMPUTE_CIRCLE_SRC "${PROJECT_ROOT}/compute_circle_mouse.wgsl")
else()
    set(COMPUTE_CIRCLE_SRC "${PROJECT_ROOT}/compute_circle_fingertip.wgsl")
endif()
configure_file(${COMPUTE_CIRCLE_SRC} "${EMBED_STAGE_DIR}/compute_circle.wgsl" COPYONLY)

foreach(SHADER ${SHADER_FILES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    if(NOT SHADER_NAME STREQUAL "compute_circle.wgsl")
        configure_file(${SHADER} "${EMBED_STAGE_DIR}/${SHADER_NAME}" COPYONLY)
    endif()
endforeach()
configure_file("${PROJECT_ROOT}/config.json" "${EMBED_STAGE_DIR}/config.json" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/public/img1.png" "${EMBED_STAGE_DIR}/img1.png" COPYONLY)

# embed data files (mounted at / in Emscripten virtual FS)
target_link_options(katara PUBLIC
    "SHELL:--embed-file ${EMBED_STAGE_DIR}@/"
)

# include directories
target_include_directories(katara PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# output as .js + .wasm (we provide our own HTML shell in public/)
set_target_properties(katara PROPERTIES SUFFIX ".js")

# copy artifacts to public/
add_custom_command(TARGET katara POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/katara.js ${CMAKE_CURRENT_SOURCE_DIR}/public/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/katara.wasm ${CMAKE_CURRENT_SOURCE_DIR}/public/)
