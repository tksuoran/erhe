set(_target erhe_commands)
add_library(${_target})
add_library(erhe::commands ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
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
    erhe_commands/xr_boolean_binding.cpp
    erhe_commands/xr_boolean_binding.hpp
    erhe_commands/xr_float_binding.cpp
    erhe_commands/xr_float_binding.hpp
    erhe_commands/xr_vector2f_binding.cpp
    erhe_commands/xr_vector2f_binding.hpp
)

target_link_libraries(${_target} PUBLIC erhe::xr)
if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    target_link_libraries(${_target} PUBLIC openxr_loader)
endif ()

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        cxxopts
        etl::etl
        fmt::fmt
        erhe::gl
        erhe::graphics
        erhe::log
        erhe::ui
        mINI
    PRIVATE
        erhe::profile
        erhe::verify
)

erhe_target_settings(${_target} "erhe")
