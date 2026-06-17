set(GPU_PUBLIC_HEADERS
    gpu.h
    core/gpu_types.h
    core/gpu_handle.h
    core/gpu_format.h
    core/gpu_device.h
    core/gpu_command.h
    core/gpu_render_pass.h
    core/gpu_buffer.h
    core/gpu_texture.h
    platform/gpu_platform.h
    platform/gpu_surface.h
    platform/gpu_swapchain.h
)

set(GPU_SOURCES
    core/gpu_device.cpp
    core/gpu_render_pass.cpp
    core/gpu_buffer.cpp
    core/gpu_texture.cpp
    core/gpu_internal.h
    core/gpu_handle_pool.h
    platform/gpu_platform_sdl.cpp
    platform/gpu_surface.cpp
    platform/gpu_swapchain.cpp
)

add_library(gpu STATIC ${GPU_PUBLIC_HEADERS} ${GPU_SOURCES})

target_include_directories(gpu
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(gpu PUBLIC slang-rhi SDL3::SDL3)

set_target_properties(gpu PROPERTIES
    PUBLIC_HEADER "${GPU_PUBLIC_HEADERS}"
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

if(MSVC)
    target_compile_options(gpu PRIVATE /W4 /WX)
else()
    target_compile_options(gpu PRIVATE -Wall -Wextra -Werror -Wno-error=unused-parameter)
endif()

target_compile_definitions(gpu PRIVATE
    $<$<CONFIG:Debug>:GPU_DEBUG=1>
)

if(GPU_INSTALL)
    install(
        TARGETS gpu
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gpu
    )

    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../gpu/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gpu
        FILES_MATCHING PATTERN "*.h"
        PATTERN "gpu_internal.h" EXCLUDE
        PATTERN "gpu_handle_pool.h" EXCLUDE
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/gpu-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/gpu-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gpu
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/gpu-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/gpu-config.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/gpu-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gpu
    )
endif()
