set(_target "imgui")

add_library(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    imconfig.h
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui.cpp
    misc/cpp/imgui_stdlib.cpp
    misc/cpp/imgui_stdlib.h
    #backends/imgui_impl_glfw.cpp
)

if (${ERHE_FONT_RASTERIZATION_LIBRARY} STREQUAL "freetype")
    add_definitions(-DIMGUI_ENABLE_FREETYPE -DIMGUI_DISABLE_STB_TRUETYPE_IMPLEMENTATION)
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        misc/freetype/imgui_freetype.cpp
        misc/freetype/imgui_freetype.h
    )
endif()

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT}/imgui)

target_link_libraries(${_target} PUBLIC glfw glm::glm)

if (${ERHE_FONT_RASTERIZATION_LIBRARY} STREQUAL "freetype")
    target_link_libraries(${_target} PUBLIC freetype)
endif()
