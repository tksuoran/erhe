set(_target erhe_commands)
add_library(${_target})
add_library(erhe::commands ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_commands/binding_desc.cpp
    erhe_commands/binding_desc.hpp
    erhe_commands/command.cpp
    erhe_commands/command.hpp
    erhe_commands/command_binding.cpp
    erhe_commands/command_binding.hpp
    erhe_commands/commands.cpp
    erhe_commands/commands.hpp
    erhe_commands/commands_log.cpp
    erhe_commands/commands_log.hpp
    erhe_commands/controller_axis_binding.cpp
    erhe_commands/controller_axis_binding.hpp
    erhe_commands/controller_button_binding.cpp
    erhe_commands/controller_button_binding.hpp
    erhe_commands/input_arguments.cpp
    erhe_commands/input_arguments.hpp
    erhe_commands/key_binding.cpp
    erhe_commands/key_binding.hpp
    erhe_commands/menu_binding.cpp
    erhe_commands/menu_binding.hpp
    erhe_commands/mouse_binding.cpp
    erhe_commands/mouse_binding.hpp
    erhe_commands/mouse_button_binding.cpp
    erhe_commands/mouse_button_binding.hpp
    erhe_commands/mouse_motion_binding.cpp
    erhe_commands/mouse_motion_binding.hpp
    erhe_commands/mouse_drag_binding.cpp
    erhe_commands/mouse_drag_binding.hpp
    erhe_commands/mouse_wheel_binding.cpp
    erhe_commands/mouse_wheel_binding.hpp
    erhe_commands/state.cpp
    erhe_commands/state.hpp
    erhe_commands/update_binding.cpp
    erhe_commands/update_binding.hpp
)

# XR action bindings exist only with OpenXR; without it erhe::commands does
# not link erhe::xr (and through it erhe::graphics).
if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_commands/xr_boolean_binding.cpp
        erhe_commands/xr_boolean_binding.hpp
        erhe_commands/xr_float_binding.cpp
        erhe_commands/xr_float_binding.hpp
        erhe_commands/xr_vector2f_binding.cpp
        erhe_commands/xr_vector2f_binding.hpp
    )
    target_link_libraries(${_target} PUBLIC erhe::xr openxr_loader)
endif ()

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        fmt::fmt
        erhe::log
        erhe::profile
        erhe::window
    PRIVATE
        erhe::verify
)

erhe_target_settings(${_target} "erhe")

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    add_subdirectory(test)
endif ()
