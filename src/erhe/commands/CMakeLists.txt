set(_target erhe_commands)
add_library(${_target})
add_library(erhe::commands ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    command.cpp
    command.hpp
    commands.cpp
    commands.hpp
    commands_log.cpp
    commands_log.hpp
    command_binding.cpp
    command_binding.hpp
    input_arguments.cpp
    input_arguments.hpp
    key_binding.cpp
    key_binding.hpp
    mouse_binding.cpp
    mouse_binding.hpp
    mouse_button_binding.cpp
    mouse_button_binding.hpp
    mouse_motion_binding.cpp
    mouse_motion_binding.hpp
    mouse_drag_binding.cpp
    mouse_drag_binding.hpp
    mouse_wheel_binding.cpp
    mouse_wheel_binding.hpp
    state.cpp
    state.hpp
    update_binding.cpp
    update_binding.hpp
)

if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        xr_boolean_binding.cpp
        xr_boolean_binding.hpp
        xr_float_binding.cpp
        xr_float_binding.hpp
        xr_vector2f_binding.cpp
        xr_vector2f_binding.hpp
    )
    target_link_libraries(${_target} PUBLIC erhe::xr openxr_loader)
endif ()

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        cxxopts
        etl::etl
        fmt::fmt
        erhe::concurrency
        erhe::gl
        erhe::graphics
        erhe::log
        erhe::toolkit
        erhe::ui
        mINI
        Microsoft.GSL::GSL
)

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
