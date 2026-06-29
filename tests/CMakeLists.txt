include("${CMAKE_SOURCE_DIR}/cmake/GpuRuntimeCopy.cmake")

function(add_gpu_test NAME)
    cmake_parse_arguments(ARG "" "" "SHADERS" ${ARGN})

    add_executable(${NAME} ${NAME}/main.c)
    target_link_libraries(${NAME} PRIVATE gpu)
    set_target_properties(${NAME} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
    )
    if(MSVC)
        target_compile_options(${NAME} PRIVATE /W4 /WX)
    else()
        target_compile_options(${NAME} PRIVATE -Wall -Wextra -Werror -std=gnu11)
    endif()

    gpu_stage_runtime_deps(${NAME})

    set(_out_dir "$<TARGET_FILE_DIR:${NAME}>")

    if(ARG_SHADERS)
        foreach(_shader ${ARG_SHADERS})
            add_custom_command(TARGET ${NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/${NAME}/${_shader}"
                    "${_out_dir}/${_shader}"
            )
        endforeach()
    endif()

    add_test(NAME ${NAME} COMMAND ${NAME})
    set_tests_properties(${NAME} PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:${NAME}>")
endfunction()

add_gpu_test(smoke)
add_gpu_test(phase1)
add_gpu_test(phase2)
add_gpu_test(phase3)
add_gpu_test(phaseA)
add_gpu_test(phaseB)
add_gpu_test(phaseC)
add_gpu_test(phaseD SHADERS reflection_test.slang)
add_gpu_test(phaseE SHADERS render_test.slang)
