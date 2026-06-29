include_guard(GLOBAL)

function(gpu_stage_runtime_deps target)
    set(_out_dir "$<TARGET_FILE_DIR:${target}>")
    set(_rhi_dir "${CMAKE_BINARY_DIR}/modules/3rd/slang-rhi")

    add_dependencies(${target} slang-rhi-copy-files)

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D SRC_DIR="${_rhi_dir}"
            -D DST_DIR="${_out_dir}"
            -P "${CMAKE_SOURCE_DIR}/cmake/CopySlangRhiRuntime.cmake"
        COMMENT "Staging slang-rhi runtime deps for ${target}"
    )

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL3-shared>" "${_out_dir}/"
    )
endfunction()
