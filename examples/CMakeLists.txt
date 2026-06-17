function(add_gpu_example NAME)
    cmake_parse_arguments(ARG "" "" "SHADERS" ${ARGN})

    add_executable(${NAME} ${NAME}/main.c)
    target_link_libraries(${NAME} PRIVATE gpu)
    set_target_properties(${NAME} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
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

add_gpu_example(00_window)
add_gpu_example(00_window_clear)
