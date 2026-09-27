add_library(opendoc_plugins OBJECT
        src/plugin_manager.cpp
        src/build_context.cpp
        src/dynamic_loader.cpp
        src/encoding.cpp
        src/manifest.cpp
        src/builtin/register.cpp
        src/builtin/search.cpp
        src/builtin/sitemap.cpp
        src/builtin/rss.cpp
)
target_include_directories(opendoc_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(opendoc_plugins PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(opendoc_plugins PUBLIC opendoc_mapper opendoc_parser opendoc_theme)
target_compile_features(opendoc_plugins PUBLIC cxx_std_20)
set_target_properties(opendoc_plugins PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
endif ()
