set(_target "erhe_ui")

set(source_list
    bitmap.hpp
    font.cpp
    font.hpp
    glyph.cpp
    glyph.hpp
    log.cpp
    log.hpp
    rectangle.hpp
)

add_library(${_target} ${source_list})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_LIST})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(
    ${_target}
    PUBLIC
    erhe_gl
    erhe_graphics
    erhe_primitive
    fmt::fmt
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

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
