cmake_minimum_required(VERSION 3.20)

project(katara_web C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

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
    ../render.cpp
    ../render_histogram.cpp
    ../sim_gpu.cpp
    ../sim_cpu.cpp
    ../config.cpp
    ../layout.cpp
    ../render_views.cpp
)

add_executable(katara ${SOURCES})

target_link_libraries(katara PRIVATE SDL2::SDL2 SDL2_image webgpu sdl2webgpu)

# suppress deprecated literal operator warnings from json.hpp
target_compile_options(katara PRIVATE -Wno-deprecated-literal-operator)

# emscripten linker flags
set(EXPORTED_FUNCTIONS "\"_main\",\"_malloc\",\"_free\",\"_setSimulatorPointer\",\"_updateFingertips\",\"_updateLineSegments\",\"_update\",\"_setInputMode\",\"_onMouseDown\",\"_onMouseUp\",\"_onMouseDrag\",\"_onMouseDragLost\",\"_getSimDomainWidth\",\"_getSimDomainHeight\",\"_getSimCellSize\",\"_setViewportTarget\",\"_setViewportVelocity\",\"_setViewportCameraBackground\",\"_setViewportRotation\",\"_getViewSource\",\"_getViewName\",\"_getViewTargetCount\",\"_setViewSource\",\"_resetViewSource\",\"_applyViewShaders\",\"_getLastShaderError\",\"_freeShaderString\",\"_reloadConfig\",\"_resetFluidField\",\"_initLayout\",\"_getLayoutPreviewJson\",\"_setSimulationPaused\",\"_setBackgroundColor\"")

target_link_options(katara PUBLIC
    "SHELL:-s WASM=1"
    "--use-port=emdawnwebgpu"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s MAXIMUM_MEMORY=4GB"
    "SHELL:-s ASYNCIFY"
    "SHELL:-s EXPORTED_FUNCTIONS=[${EXPORTED_FUNCTIONS}]"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\",\"HEAPF32\",\"FS\",\"IDBFS\",\"callMain\",\"UTF8ToString\"]"
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_NAME=\"createKataraModule\""
    "SHELL:-s FORCE_FILESYSTEM=1"
    "-lidbfs.js"
)

# stage shader and config files for embedding
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(EMBED_STAGE_DIR ${CMAKE_CURRENT_BINARY_DIR}/embed)
set(EMBED_STAMP ${CMAKE_CURRENT_BINARY_DIR}/embed.stamp)

file(GLOB SHADER_FILES CONFIGURE_DEPENDS "${PROJECT_ROOT}/*.wgsl")
file(GLOB VIEW_FILES CONFIGURE_DEPENDS
    "${PROJECT_ROOT}/views/*.wgsl"
    "${PROJECT_ROOT}/views/*.json"
)

set(EMBED_SOURCES
    ${SHADER_FILES}
    ${VIEW_FILES}
    "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
    "${PROJECT_ROOT}/img1.png"
)

set(STAGE_EMBED_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${EMBED_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${EMBED_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_ROOT}/views" "${EMBED_STAGE_DIR}/views"
)
foreach(SHADER ${SHADER_FILES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    list(APPEND STAGE_EMBED_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER}" "${EMBED_STAGE_DIR}/${SHADER_NAME}"
    )
endforeach()
list(APPEND STAGE_EMBED_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.json" "${EMBED_STAGE_DIR}/config.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_ROOT}/img1.png" "${EMBED_STAGE_DIR}/img1.png"
    COMMAND ${CMAKE_COMMAND} -E touch "${EMBED_STAMP}"
)

add_custom_command(
    OUTPUT "${EMBED_STAMP}"
    ${STAGE_EMBED_COMMANDS}
    DEPENDS ${EMBED_SOURCES}
    COMMENT "Staging files for Emscripten --embed-file"
    VERBATIM
)
add_custom_target(katara_stage_embed DEPENDS "${EMBED_STAMP}")
add_dependencies(katara katara_stage_embed)
set_property(TARGET katara APPEND PROPERTY LINK_DEPENDS "${EMBED_STAMP}")

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
