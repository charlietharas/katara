cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(katara)

# using a slightly different build process than
# https://eliemichel.github.io/LearnWebGPU/appendices/using-sdl.html
find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_program(HOMEBREW_EXECUTABLE brew)
    if(HOMEBREW_EXECUTABLE)
        execute_process(
            COMMAND "${HOMEBREW_EXECUTABLE}" --prefix libomp
            OUTPUT_VARIABLE HOMEBREW_LIBOMP_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE HOMEBREW_LIBOMP_RESULT
        )
        if(HOMEBREW_LIBOMP_RESULT EQUAL 0 AND IS_DIRECTORY "${HOMEBREW_LIBOMP_PREFIX}")
            list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_LIBOMP_PREFIX}")
        endif()
    endif()
endif()
find_package(OpenMP REQUIRED)

# use dawn as webgpu backend
# see webgpu/WebGPU_dawn/README.md and "/LICENSE.txt
# many thanks to Élie Michel :)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../webgpu/WebGPU_dawn webgpu/WebGPU_dawn)
# using sdl2 with webgpu
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../webgpu/sdl2webgpu-main webgpu/sdl2webgpu-main)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

target_link_libraries(katara PRIVATE SDL2::SDL2 PkgConfig::SDL2_IMAGE webgpu sdl2webgpu OpenMP::OpenMP_CXX)
target_include_directories(katara PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_copy_webgpu_binaries(katara)

# Desktop config mirrors web/config.json; only config/desktop.overrides.json differs.
find_program(NODE_EXE node)
if(NODE_EXE)
    execute_process(
        COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/sync-desktop-config.mjs
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
        RESULT_VARIABLE DESKTOP_CONFIG_SYNC_RESULT
    )
    if(NOT DESKTOP_CONFIG_SYNC_RESULT EQUAL 0)
        message(WARNING "desktop config sync failed (exit ${DESKTOP_CONFIG_SYNC_RESULT})")
    endif()
else()
    message(WARNING "node not found; desktop/config.json may be stale vs web/config.json")
endif()

# Runtime assets are loaded via relative paths — run ./katara from this build directory.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../img1.png ${CMAKE_CURRENT_BINARY_DIR}/img1.png COPYONLY)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../views DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# vertex + compute shaders (GPU sim/render load these at runtime)
file(GLOB SHADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../*.wgsl")
foreach(SHADER ${SHADER_FILES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    if(NOT SHADER_NAME STREQUAL "fragment.wgsl"
       AND NOT SHADER_NAME STREQUAL "fragment_uniform.wgsl")
        configure_file(${SHADER} ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME} COPYONLY)
    endif()
endforeach()

# modular fragment shader files
set(MODULAR_FRAGMENT_FILES
    fragment_uniform.wgsl
    fragment.wgsl
)
foreach(MODULAR_FILE ${MODULAR_FRAGMENT_FILES})
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../${MODULAR_FILE} ${CMAKE_CURRENT_BINARY_DIR}/${MODULAR_FILE} COPYONLY)
endforeach()
