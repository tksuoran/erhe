set(_target "erhe_ui")
add_library(${_target})
add_library(erhe::ui ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_ui/bitmap.hpp
    erhe_ui/font.cpp
    erhe_ui/font.hpp
    erhe_ui/glyph.cpp
    erhe_ui/glyph.hpp
    erhe_ui/ui_log.cpp
    erhe_ui/ui_log.hpp
    erhe_ui/rectangle.hpp
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_LIST})
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
target_link_libraries(
    ${_target}
    PUBLIC
        erhe::gl
        erhe::graphics
        erhe::primitive
        fmt::fmt
        glm::glm
        glfw
    PRIVATE
        erhe::log
        erhe::profile
        Microsoft.GSL::GSL
        RectangleBinPack::RectangleBinPack
)
if (${ERHE_FONT_RASTERIZATION_LIBRARY} STREQUAL "freetype")
    target_link_libraries(${_target} PRIVATE freetype)
endif ()
if (${ERHE_TEXT_LAYOUT_LIBRARY} STREQUAL "harfbuzz")
    target_link_libraries(${_target} PRIVATE harfbuzz)
endif ()
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
