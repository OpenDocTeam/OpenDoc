# Re-run configure when an asset changes so the embedded copy stays fresh.
set(_asset_files
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/default/layout.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/default/css/base.css"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/default/js/theme.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/default/js/search.js"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_asset_files})

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/default/layout.html" _layout_html)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/default/css/base.css" _base_css)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/default/js/theme.js" _theme_js)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/default/js/search.js" _search_js)

set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_gen_dir}")

set(_header "\
#pragma once\n\
#include <string_view>\n\
namespace opendoc::theme::embedded {\n\
inline constexpr std::string_view default_layout_html = R\"OD_LAYOUT(${_layout_html})OD_LAYOUT\";\n\
inline constexpr std::string_view default_base_css = R\"OD_CSS(${_base_css})OD_CSS\";\n\
inline constexpr std::string_view default_theme_js = R\"OD_JS(${_theme_js})OD_JS\";\n\
inline constexpr std::string_view default_search_js = R\"OD_SEARCH(${_search_js})OD_SEARCH\";\n\
}\n\
")

string(FIND "${_layout_html}" ")OD_LAYOUT\"" _bad1)
string(FIND "${_base_css}" ")OD_CSS\"" _bad2)
string(FIND "${_theme_js}" ")OD_JS\"" _bad3)
string(FIND "${_search_js}" ")OD_SEARCH\"" _bad4)
if (NOT _bad1 EQUAL -1 OR NOT _bad2 EQUAL -1 OR NOT _bad3 EQUAL -1 OR NOT _bad4 EQUAL -1)
    message(FATAL_ERROR "theme asset contains raw-string delimiter => change delimiters")
endif ()

# Write via COPYONLY so unchanged content does not touch the header timestamp
# (which would rebuild the whole theme on every configure).
file(WRITE "${_gen_dir}/theme_assets.hpp.in" "${_header}")
configure_file("${_gen_dir}/theme_assets.hpp.in" "${_gen_dir}/theme_assets.hpp" COPYONLY)

add_library(opendoc_theme OBJECT
        src/template_engine.cpp
        src/theme.cpp
)
target_include_directories(opendoc_theme
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
        ${_gen_dir}
)
target_link_libraries(opendoc_theme PUBLIC opendoc_mapper opendoc_parser)
target_compile_features(opendoc_theme PUBLIC cxx_std_20)
set_target_properties(opendoc_theme PROPERTIES POSITION_INDEPENDENT_CODE ON)
