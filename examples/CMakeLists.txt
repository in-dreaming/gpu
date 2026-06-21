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
    if(EXISTS "${GPU_SPONZA_ROOT}/sponza.obj" AND EXISTS "${GPU_SPONZA_ROOT}/sponza.mtl")
        add_custom_command(TARGET 21_sponza_data_driven POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${GPU_SPONZA_ROOT}"
                "$<TARGET_FILE_DIR:21_sponza_data_driven>/Sponza"
        )
        if(GPU_INSTALL)
            install(DIRECTORY "${GPU_SPONZA_ROOT}/" DESTINATION ${CMAKE_INSTALL_BINDIR}/Sponza)
        endif()
    else()
        message(WARNING "GPU_SPONZA_ROOT is set, but sponza.obj/sponza.mtl were not found: ${GPU_SPONZA_ROOT}")
    endif()
endif()

if(GPU_INSTALL)
    install(TARGETS 21_sponza_data_driven RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    copy_gpu_example_runtime_assets(21_sponza_data_driven)
endif()
