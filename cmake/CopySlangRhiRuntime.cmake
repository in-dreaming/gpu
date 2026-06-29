# Copy runtime libraries staged by slang-rhi-copy-files into a target output dir.
# Required variables: SRC_DIR, DST_DIR

if(NOT SRC_DIR OR NOT DST_DIR)
    message(FATAL_ERROR "CopySlangRhiRuntime.cmake requires SRC_DIR and DST_DIR")
endif()

file(MAKE_DIRECTORY "${DST_DIR}/D3D12")

file(GLOB _runtime_libs
    "${SRC_DIR}/*.dll"
    "${SRC_DIR}/*.so"
    "${SRC_DIR}/*.dylib"
)
foreach(_src IN LISTS _runtime_libs)
    get_filename_component(_name "${_src}" NAME)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${DST_DIR}/${_name}"
    )
endforeach()

if(IS_DIRECTORY "${SRC_DIR}/D3D12")
    file(GLOB _d3d12_libs "${SRC_DIR}/D3D12/*")
    foreach(_src IN LISTS _d3d12_libs)
        get_filename_component(_name "${_src}" NAME)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${DST_DIR}/D3D12/${_name}"
        )
    endforeach()
endif()
