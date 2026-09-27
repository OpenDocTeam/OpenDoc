add_library(opendoc_parser OBJECT
        src/front_matter.cpp
        src/markdown_parser.cpp
        src/detail/inline_parser.cpp
        src/detail/block_parser.cpp
        src/detail/block_constructs.cpp
        src/html_renderer.cpp
        src/plain_text.cpp
        src/syntax_highlight.cpp
)
target_include_directories(opendoc_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opendoc_parser PUBLIC yaml-cpp::yaml-cpp)
target_compile_features(opendoc_parser PUBLIC cxx_std_20)
set_target_properties(opendoc_parser PROPERTIES POSITION_INDEPENDENT_CODE ON)
