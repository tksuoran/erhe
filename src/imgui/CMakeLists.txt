set(source_list
    imconfig.h
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui.cpp
    misc/cpp/imgui_stdlib.cpp
    misc/cpp/imgui_stdlib.h
    backends/imgui_impl_glfw.cpp
)

if (${ERHE_FONT_RASTERIZATION_LIBRARY} STREQUAL "freetype")
    add_definitions(-DIMGUI_ENABLE_FREETYPE)
    list(APPEND source_list
        misc/freetype/imgui_freetype.cpp
        misc/freetype/imgui_freetype.h
    )
endif()

add_library(imgui ${source_list})

target_include_directories(imgui
    PUBLIC
        ${ERHE_INCLUDE_ROOT}/imgui
)

target_link_libraries(imgui PUBLIC glfw)

if (${ERHE_FONT_RASTERIZATION_LIBRARY} STREQUAL "freetype")
    target_link_libraries(imgui PUBLIC freetype)
endif()

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
