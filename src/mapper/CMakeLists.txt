add_library(opendoc_mapper OBJECT
        src/config.cpp
        src/scanner.cpp
        src/site_tree.cpp
        src/link_resolver.cpp
)
target_include_directories(opendoc_mapper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opendoc_mapper PUBLIC yaml-cpp::yaml-cpp)
target_compile_features(opendoc_mapper PUBLIC cxx_std_20)
set_target_properties(opendoc_mapper PROPERTIES POSITION_INDEPENDENT_CODE ON)
