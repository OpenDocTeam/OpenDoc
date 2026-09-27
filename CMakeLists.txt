cmake_minimum_required(VERSION 3.20)
project(OpenDoc VERSION 1.0.0 LANGUAGES CXX)

string(TIMESTAMP OPENDOC_BUILD_DATE "%Y-%m-%d" UTC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(yaml-cpp REQUIRED)

option(OPENDOC_STATIC_YAML_CPP "Link the static libyaml-cpp.a instead of the shared library" OFF)
if(OPENDOC_STATIC_YAML_CPP)
    set(_yaml_suffixes_save "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    find_library(OPENDOC_YAML_CPP_STATIC NAMES yaml-cpp)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${_yaml_suffixes_save}")
    if(OPENDOC_YAML_CPP_STATIC AND TARGET yaml-cpp::yaml-cpp)
        foreach(_cfg RELEASE RELWITHDEBINFO MINSIZEREL DEBUG NOCONFIG)
            set_property(TARGET yaml-cpp::yaml-cpp
                    PROPERTY IMPORTED_LOCATION_${_cfg} "${OPENDOC_YAML_CPP_STATIC}")
        endforeach()
        message(STATUS "Linking yaml-cpp statically: ${OPENDOC_YAML_CPP_STATIC}")
    else()
        message(WARNING "OPENDOC_STATIC_YAML_CPP=ON but libyaml-cpp.a was not found; "
                "the binary will depend on the shared libyaml-cpp")
    endif()
endif()

add_subdirectory(src/mapper)
add_subdirectory(src/parser)
add_subdirectory(src/theme)
add_subdirectory(src/plugins)

# Shared SDK: one copy of RTTI/vtables for the executable and all plugin.
add_library(opendoc_sdk SHARED
        $<TARGET_OBJECTS:opendoc_mapper>
        $<TARGET_OBJECTS:opendoc_parser>
        $<TARGET_OBJECTS:opendoc_theme>
        $<TARGET_OBJECTS:opendoc_plugins>
)
target_include_directories(opendoc_sdk
        PUBLIC
        ${CMAKE_SOURCE_DIR}/src/mapper/include
        ${CMAKE_SOURCE_DIR}/src/parser/include
        ${CMAKE_SOURCE_DIR}/src/theme/include
        ${CMAKE_SOURCE_DIR}/src/plugins/include
)
target_link_libraries(opendoc_sdk PUBLIC yaml-cpp::yaml-cpp)
target_compile_features(opendoc_sdk PUBLIC cxx_std_20)
set_target_properties(opendoc_sdk PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
)
if (MINGW)
    target_link_options(opendoc_sdk PRIVATE -Wl,--export-all-symbols)
endif ()
if (UNIX AND NOT APPLE)
    target_link_libraries(opendoc_sdk PUBLIC ${CMAKE_DL_LIBS})
endif ()

add_subdirectory(src/app)
add_subdirectory(examples/plugin)
