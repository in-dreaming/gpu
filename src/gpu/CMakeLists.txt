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
    core/gpu_pipeline.h
    platform/gpu_platform.h
    platform/gpu_surface.h
    platform/gpu_swapchain.h
    capability/gpu_capabilities.h
    debug/gpu_debug.h
    sync/gpu_fence.h
    resource/gpu_barrier.h
    resource/gpu_resource_manager.h
    resource/gpu_readback.h
    resource/gpu_streaming_manager.h
    resource/gpu_sparse_resource.h
    shader/gpu_shader_compiler.h
    shader/gpu_slang_module.h
    shader/gpu_shader_cache.h
    reflection/gpu_type_info.h
    reflection/gpu_reflection_cache.h
    reflection/gpu_meta_gen.h
    pipeline/gpu_pipeline_state.h
    pipeline/gpu_pipeline_cache.h
    pipeline/gpu_cmd_bind.h
    pipeline/gpu_mesh_pipeline.h
    layout/gpu_shader_object.h
    layout/gpu_parameter_block.h
    queue/gpu_multi_queue.h
    bindless/gpu_bindless_heap.h
    # Phase 3 - Capability
    capability/gpu_feature_gating.h
    capability/gpu_feature_info.h
    capability/gpu_fallback_registry.h
    # Phase B - Lifecycle
    resource/gpu_frame_context.h
    # Phase 3 - AI
    ai/gpu_tensor.h
    ai/gpu_matrix_pipeline.h
    ai/gpu_neural.h
    # Phase 3 - Work Graph
    workgraph/gpu_work_graph.h
    # Phase 3 - Ray Tracing
    raytracing/gpu_raytracing.h
)

set(GPU_SOURCES
    core/gpu_device.cpp
    core/gpu_render_pass.cpp
    core/gpu_buffer.cpp
    core/gpu_texture.cpp
    core/gpu_pipeline.cpp
    core/gpu_internal.h
    core/gpu_handle_pool.h
    platform/gpu_platform_sdl.cpp
    platform/gpu_surface.cpp
    platform/gpu_swapchain.cpp
    capability/gpu_capabilities.cpp
    debug/gpu_debug.cpp
    sync/gpu_fence.cpp
    resource/gpu_barrier.cpp
    resource/gpu_resource_manager.cpp
    resource/gpu_readback.cpp
    resource/gpu_streaming_manager.cpp
    resource/gpu_sparse_resource.cpp
    resource/gpu_frame_context.cpp
    shader/gpu_shader_compiler.cpp
    shader/gpu_slang_module.cpp
    shader/gpu_shader_cache.cpp
    reflection/gpu_type_info.cpp
    reflection/gpu_reflection_cache.cpp
    reflection/gpu_meta_gen.cpp
    pipeline/gpu_pipeline_state.cpp
    pipeline/gpu_pipeline_cache.cpp
    pipeline/gpu_cmd_bind.cpp
    pipeline/gpu_mesh_pipeline.cpp
    layout/gpu_parameter_block.cpp
    layout/gpu_shader_object.cpp
    queue/gpu_multi_queue.cpp
    bindless/gpu_bindless_heap.cpp
    bindless/gpu_descriptor_alloc.h
    # Phase 3 - Capability
    capability/gpu_feature_gating.cpp
    capability/gpu_fallback_registry.cpp
    # Phase 3 - AI
    ai/gpu_tensor.cpp
    ai/gpu_matrix_pipeline.cpp
    ai/gpu_neural.cpp
    # Phase 3 - Work Graph
    workgraph/gpu_work_graph.cpp
    # Phase 3 - Ray Tracing
    raytracing/gpu_raytracing.cpp
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
