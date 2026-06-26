function(gpu_resolve_sponza_root)
    set(_default "${CMAKE_SOURCE_DIR}/modules/3rd/Sponza")
    if(NOT EXISTS "${_default}/sponza.obj" OR NOT EXISTS "${_default}/sponza.mtl")
        return()
    endif()

    if(GPU_SPONZA_ROOT STREQUAL "" OR NOT EXISTS "${GPU_SPONZA_ROOT}/sponza.obj")
        set(GPU_SPONZA_ROOT "${_default}" PARENT_SCOPE)
    endif()
endfunction()

function(gpu_setup_sponza_assets TARGET)
    if(NOT GPU_SPONZA_ROOT)
        return()
    endif()

    if(EXISTS "${GPU_SPONZA_ROOT}/sponza.obj" AND EXISTS "${GPU_SPONZA_ROOT}/sponza.mtl")
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${TARGET}>/Sponza"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${GPU_SPONZA_ROOT}"
                "$<TARGET_FILE_DIR:${TARGET}>/Sponza"
        )
        if(GPU_INSTALL)
            install(DIRECTORY "${GPU_SPONZA_ROOT}/"
                DESTINATION ${CMAKE_INSTALL_BINDIR}/Sponza
                PATTERN ".git" EXCLUDE
            )
        endif()
    else()
        message(WARNING "GPU_SPONZA_ROOT is set, but sponza.obj/sponza.mtl were not found: ${GPU_SPONZA_ROOT}")
    endif()
endfunction()
