add_executable(opendoc
        main.cpp
        cli.cpp
        log.cpp
        builder.cpp
        link_check.cpp
        page_renderer.cpp
        serve/http_server.cpp
)
target_include_directories(opendoc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(opendoc PRIVATE opendoc_sdk)
target_compile_features(opendoc PRIVATE cxx_std_20)
target_compile_definitions(opendoc PRIVATE
        OPENDOC_VERSION="${PROJECT_VERSION}"
        OPENDOC_BUILD_DATE="${OPENDOC_BUILD_DATE}"
)

if (WIN32)
    target_link_libraries(opendoc PRIVATE ws2_32)
endif ()
