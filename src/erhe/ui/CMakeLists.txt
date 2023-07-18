set(_target "erhe_ui")
add_library(${_target})
add_library(erhe::ui ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    bitmap.hpp
    font.cpp
    font.hpp
    glyph.cpp
    glyph.hpp
    ui_log.cpp
    ui_log.hpp
    rectangle.hpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_LIST})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

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
        Microsoft.GSL::GSL
        RectangleBinPack::RectangleBinPack
)

if (${ERHE_FONT_RASTERIZATION_LIBRARY} STREQUAL "freetype")
    target_link_libraries(${_target} PRIVATE freetype)
endif ()

if (${ERHE_TEXT_LAYOUT_LIBRARY} STREQUAL "harfbuzz")
    target_link_libraries(${_target} PRIVATE harfbuzz)
endif ()

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
