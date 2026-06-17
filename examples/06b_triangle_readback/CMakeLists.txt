add_executable(06b_triangle_readback main.c)
target_link_libraries(06b_triangle_readback gpu)
set_target_properties(06b_triangle_readback PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
