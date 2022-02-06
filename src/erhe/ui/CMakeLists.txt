set(_target "erhe_ui")

add_library(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    bitmap.hpp
    font.cpp
    font.hpp
    glyph.cpp
    glyph.hpp
    log.cpp
    log.hpp
    rectangle.hpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_LIST})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(
    ${_target}
    PUBLIC
    erhe_gl
    erhe_graphics
    erhe_primitive
    fmt::fmt
    ghc_filesystem
    glm::glm
    glfw
    Microsoft.GSL::GSL
    RectangleBinPack::RectangleBinPack
)

if (${ERHE_FONT_RASTERIZATION_LIBRARY} STREQUAL "freetype")
    target_link_libraries(${_target} PRIVATE freetype)
endif ()

if (${ERHE_TEXT_LAYOUT_LIBRARY} STREQUAL "harfbuzz")
    target_link_libraries(${_target} PRIVATE harfbuzz)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
