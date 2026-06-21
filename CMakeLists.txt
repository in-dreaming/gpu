cmake_minimum_required(VERSION 3.24)
project(gpu LANGUAGES C CXX VERSION 0.1.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(GPU_ENABLE_VULKAN "Enable Vulkan backend" ON)
option(GPU_ENABLE_D3D12 "Enable D3D12 backend" ON)
option(GPU_ENABLE_METAL "Enable Metal backend" OFF)
option(GPU_ENABLE_WEBGPU "Enable WebGPU backend" OFF)
option(GPU_BUILD_EXAMPLES "Build examples" ON)
option(GPU_BUILD_TESTS "Build tests" ON)
option(GPU_INSTALL "Enable install rules" ON)
set(GPU_SPONZA_ROOT "" CACHE PATH "Optional path to jimmiebergmann/Sponza assets for example 21")

include(CTest)

if(NOT GPU_BUILD_TESTS)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
endif()

set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(modules/3rd/sdl)

set(SLANG_RHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(modules/3rd/slang-rhi)

add_subdirectory(src/gpu)

if(GPU_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(GPU_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(GPU_INSTALL)
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
        FILES_MATCHING PATTERN "*.md"
    )
endif()
