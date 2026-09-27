add_library(hello_plugin SHARED
        hello_plugin.cpp
)
target_link_libraries(hello_plugin PRIVATE opendoc_sdk)
target_compile_features(hello_plugin PRIVATE cxx_std_20)
set_target_properties(hello_plugin PROPERTIES
        OUTPUT_NAME "hello"
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/hello"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/hello"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/hello"
)

add_custom_command(TARGET hello_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json"
        "${CMAKE_BINARY_DIR}/plugins/hello/plugin.json"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/theme"
        "${CMAKE_BINARY_DIR}/plugins/hello/theme"
        COMMENT "Staging hello plugin folder"
)
