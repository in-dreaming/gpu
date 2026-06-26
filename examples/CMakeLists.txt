function(add_gpu_example NAME)
    cmake_parse_arguments(ARG "" "" "SHADERS" ${ARGN})

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${NAME}/main.cpp")
        add_executable(${NAME} ${NAME}/main.cpp)
    else()
        add_executable(${NAME} ${NAME}/main.c)
    endif()
    target_link_libraries(${NAME} PRIVATE gpu)
    set_target_properties(${NAME} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        VS_DEBUGGER_WORKING_DIRECTORY "${_out_dir}"
        VS_DEBUGGER_ENVIRONMENT "PATH=${_rhi_dir}\;$<TARGET_FILE_DIR:SDL3-shared>\;%PATH%"
    )
    if(MSVC)
        target_compile_options(${NAME} PRIVATE /W4 /WX)
    else()
        target_compile_options(${NAME} PRIVATE -Wall -Wextra -Werror -pedantic)
    endif()

    add_dependencies(${NAME} slang-rhi-copy-files)

    set(_out_dir "$<TARGET_FILE_DIR:${NAME}>")
    set(_rhi_dir "${CMAKE_BINARY_DIR}/modules/3rd/slang-rhi")

    add_custom_command(TARGET ${NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}/D3D12"
    )

    foreach(_dll
        slang.dll slang-compiler.dll slang-glslang.dll slang-llvm.dll slang-rt.dll
        dxcompiler.dll dxil.dll WinPixEventRuntime.dll dawn.dll
    )
        add_custom_command(TARGET ${NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_rhi_dir}/${_dll}" "${_out_dir}/${_dll}"
        )
    endforeach()

    foreach(_dll D3D12/D3D12Core.dll D3D12/d3d12SDKLayers.dll)
        add_custom_command(TARGET ${NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_rhi_dir}/${_dll}" "${_out_dir}/${_dll}"
        )
    endforeach()

    add_custom_command(TARGET ${NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL3-shared>" "${_out_dir}/"
    )

    if(ARG_SHADERS)
        foreach(_shader ${ARG_SHADERS})
            add_custom_command(TARGET ${NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/${NAME}/${_shader}"
                    "${_out_dir}/${_shader}"
            )
        endforeach()
    endif()
endfunction()

function(copy_gpu_example_runtime_assets TARGET)
    set(_out_dir "$<TARGET_FILE_DIR:${TARGET}>")
    set(_rhi_dir "${CMAKE_BINARY_DIR}/modules/3rd/slang-rhi")

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}/D3D12"
    )

    foreach(_dll
        slang.dll slang-compiler.dll slang-glslang.dll slang-llvm.dll slang-rt.dll
        dxcompiler.dll dxil.dll WinPixEventRuntime.dll dawn.dll
    )
        install(FILES "${_rhi_dir}/${_dll}" DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
    endforeach()

    foreach(_dll D3D12/D3D12Core.dll D3D12/d3d12SDKLayers.dll)
        install(FILES "${_rhi_dir}/${_dll}" DESTINATION ${CMAKE_INSTALL_BINDIR}/D3D12 OPTIONAL)
    endforeach()

    install(FILES "$<TARGET_FILE:SDL3-shared>" DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()

add_gpu_example(00_window)
add_gpu_example(00_window_clear)
add_gpu_example(01_device_init)
add_gpu_example(06_triangle SHADERS triangle.slang)
add_gpu_example(06b_triangle_readback SHADERS triangle.slang)
add_gpu_example(07_compute_pipeline SHADERS prefix_sum.slang)

# Phase C - Render Graph Examples
add_gpu_example(22_triangle_graph SHADERS triangle.slang)
add_gpu_example(23_readback_graph SHADERS triangle.slang)
add_gpu_example(25_async_compute_graph SHADERS fill.slang triangle.slang)
if(MSVC)
    set_target_properties(25_async_compute_graph PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
endif()

# Phase 3 Examples
add_gpu_example(13_backend_switch)
add_gpu_example(14_feature_gating)
add_gpu_example(15_tensor_matmul)
add_gpu_example(16_neural_render)
add_gpu_example(17_workgraph)
add_gpu_example(18_raytracing)
add_gpu_example(19_full_demo)
add_gpu_example(20_tensor_data_ops)
add_gpu_example(21_sponza_data_driven)
target_include_directories(21_sponza_data_driven PRIVATE
    "${CMAKE_SOURCE_DIR}/modules/3rd/slang-rhi/external/stb"
)

if(GPU_SPONZA_ROOT)
    gpu_setup_sponza_assets(21_sponza_data_driven)
endif()

if(GPU_INSTALL)
    install(TARGETS 21_sponza_data_driven RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    copy_gpu_example_runtime_assets(21_sponza_data_driven)
endif()

# Demo 24: Sponza with Render Graph + CSM + 1K Lights + SSGI
set(24_SPONZA_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/core/camera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/core/dir_light_control.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/core/shadow_math.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/core/simple_scene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/core/demo_scene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/core/sponza_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/render/render_resources.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/render/material_textures.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/render/pipelines.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/render/pass_bindings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/render/pass_callbacks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/render/frame_graph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/render/frame_update.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/render/placed_lights.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/render/shadow_diag.cpp
)
set(24_SPONZA_SHADERS
    shadow.slang gbuffer.slang forward.slang ssgi.slang light_cull.slang
)
add_executable(24_sponza_graph ${24_SPONZA_SOURCES})
target_link_libraries(24_sponza_graph PRIVATE gpu)
set_target_properties(24_sponza_graph PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    VS_DEBUGGER_WORKING_DIRECTORY "${_out_dir}"
    VS_DEBUGGER_ENVIRONMENT "PATH=${_rhi_dir}\;$<TARGET_FILE_DIR:SDL3-shared>\;%PATH%"
)
if(MSVC)
    target_compile_options(24_sponza_graph PRIVATE /W4 /WX)
else()
    target_compile_options(24_sponza_graph PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
target_include_directories(24_sponza_graph PRIVATE
    "${CMAKE_SOURCE_DIR}/examples/24_sponza_graph"
    "${CMAKE_SOURCE_DIR}/modules/3rd/slang-rhi/external/stb"
)
add_dependencies(24_sponza_graph slang-rhi-copy-files)

set(_out_dir "$<TARGET_FILE_DIR:24_sponza_graph>")
set(_rhi_dir "${CMAKE_BINARY_DIR}/modules/3rd/slang-rhi")

add_custom_command(TARGET 24_sponza_graph POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}/D3D12"
)
foreach(_dll
    slang.dll slang-compiler.dll slang-glslang.dll slang-llvm.dll slang-rt.dll
    dxcompiler.dll dxil.dll WinPixEventRuntime.dll dawn.dll
)
    add_custom_command(TARGET 24_sponza_graph POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_rhi_dir}/${_dll}" "${_out_dir}/${_dll}"
    )
endforeach()
foreach(_dll D3D12/D3D12Core.dll D3D12/d3d12SDKLayers.dll)
    add_custom_command(TARGET 24_sponza_graph POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_rhi_dir}/${_dll}" "${_out_dir}/${_dll}"
    )
endforeach()
add_custom_command(TARGET 24_sponza_graph POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL3-shared>" "${_out_dir}/"
)
foreach(_shader shadow.slang gbuffer.slang forward.slang ssgi.slang light_cull.slang)
    add_custom_command(TARGET 24_sponza_graph POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/24_sponza_graph/shaders/${_shader}"
            "${_out_dir}/${_shader}"
    )
endforeach()

if(GPU_SPONZA_ROOT)
    gpu_setup_sponza_assets(24_sponza_graph)
endif()

if(GPU_INSTALL)
    install(TARGETS 24_sponza_graph RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    copy_gpu_example_runtime_assets(24_sponza_graph)
endif()
