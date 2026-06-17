set(SMOKE_SOURCES smoke/main.c)

add_executable(smoke_test ${SMOKE_SOURCES})
target_link_libraries(smoke_test PRIVATE gpu)
set_target_properties(smoke_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)
if(MSVC)
    target_compile_options(smoke_test PRIVATE /W4 /WX)
else()
    target_compile_options(smoke_test PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_dependencies(smoke_test slang-rhi-copy-files)

set(_out_dir "$<TARGET_FILE_DIR:smoke_test>")
set(_rhi_dir "${CMAKE_BINARY_DIR}/modules/3rd/slang-rhi")

add_custom_command(TARGET smoke_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}/D3D12"
)

foreach(_dll
    slang.dll slang-compiler.dll slang-glslang.dll slang-llvm.dll slang-rt.dll
    dxcompiler.dll dxil.dll WinPixEventRuntime.dll dawn.dll
)
    add_custom_command(TARGET smoke_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_rhi_dir}/${_dll}" "${_out_dir}/${_dll}"
    )
endforeach()

foreach(_dll D3D12/D3D12Core.dll D3D12/d3d12SDKLayers.dll)
    add_custom_command(TARGET smoke_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_rhi_dir}/${_dll}" "${_out_dir}/${_dll}"
    )
endforeach()

add_custom_command(TARGET smoke_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL3-shared>" "${_out_dir}/"
)

add_test(NAME smoke_test COMMAND smoke_test)
