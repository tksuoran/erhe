set(_target "erhe_scene_renderer")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_library(${_target})
add_library(erhe::scene_renderer ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES

    camera_buffer.cpp
    camera_buffer.hpp
    forward_renderer.cpp
    forward_renderer.hpp
    joint_buffer.cpp
    joint_buffer.hpp
    light_buffer.cpp
    light_buffer.hpp
    material_buffer.cpp
    material_buffer.hpp
    primitive_buffer.cpp
    primitive_buffer.hpp
    program_interface.cpp
    program_interface.hpp
    scene_renderer_log.cpp
    scene_renderer_log.hpp
    shadow_renderer.cpp
    shadow_renderer.hpp
    #scene_renderer_message.hpp
    #scene_renderer_message_bus.cpp
    #scene_renderer_message_bus.hpp
    #scene_renderer_settings.hpp
)

target_link_libraries(
    ${_target}
    PUBLIC
        erhe::renderer
    PRIVATE
        erhe::concurrency
        erhe::gl
        erhe::graphics
        erhe::log
        erhe::message_bus
        erhe::primitive
        erhe::scene
        erhe::toolkit
)

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD                  20
    CXX_STANDARD_REQUIRED         YES
    CXX_EXTENSIONS                NO
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
