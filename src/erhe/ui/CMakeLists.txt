set(_target "erhe_ui")

set(source_list
    #action.hpp
    #area.cpp
    #area.hpp
    bitmap.hpp
    #button.cpp
    #button.hpp
    #choice.cpp
    #choice.hpp
    #color_picker.cpp
    #color_picker.hpp
    #context.cpp
    #dock.cpp
    #dock.hpp
    font.cpp
    font.hpp
    glyph.cpp
    glyph.hpp
    #gui_renderer.cpp
    #gui_renderer.hpp
    #layer.hpp
    log.cpp
    log.hpp
    #menulist.cpp
    #menulist.hpp
    #ninepatch_style.cpp
    #ninepatch_style.hpp
    #ninepatch.cpp
    #ninepatch.hpp
    #popup.cpp
    #popup.hpp
    #push_button.hpp
    rectangle.hpp
    #slider.cpp
    #slider.hpp
    #style.hpp
    #text_buffer.cpp
    #text_buffer.hpp
)

add_library(${_target} ${source_list})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_LIST})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        erhe_gl
        erhe_graphics
        erhe_primitive
        freetype
        fmt::fmt
        glm::glm
        glfw
        harfbuzz
        Microsoft.GSL::GSL
        RectangleBinPack::RectangleBinPack
)

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
