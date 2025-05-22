set(_target "imgui_gradient")
add_library(${_target})
add_library(imgui_gradient::imgui_gradient ALIAS ${_target})
target_link_libraries(${_target} PUBLIC imgui)
target_include_directories(${_target} PUBLIC include)
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    src/color_conversions.cpp
    src/color_conversions.hpp
    src/ColorRGBA.hpp
    src/extra_widgets.cpp
    src/extra_widgets.hpp
    src/Flags.hpp
    src/Gradient.cpp
    src/Gradient.hpp
    src/GradientWidget.cpp
    src/GradientWidget.hpp
    src/HoverChecker.cpp
    src/HoverChecker.hpp
    src/imgui_draw.cpp
    src/imgui_draw.hpp
    src/imgui_internal.hpp
    src/internal.hpp
    src/Interpolation.hpp
    src/Mark.hpp
    src/MarkId.hpp
    src/maybe_disabled.cpp
    src/maybe_disabled.hpp
    src/RelativePosition.cpp
    src/RelativePosition.hpp
    src/Settings.hpp
    src/Utils.hpp
    src/WrapMode.hpp
    include/imgui_gradient/imgui_gradient.hpp
)
#erhe_target_settings(${_target} "imgui_gradient")
