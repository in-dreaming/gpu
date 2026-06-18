add_executable(20_tensor_data_ops main.c)
target_link_libraries(20_tensor_data_ops PRIVATE gpu)
set_target_properties(20_tensor_data_ops PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)
