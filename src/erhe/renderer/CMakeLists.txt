set(_target "erhe_renderer")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_library(${_target})
add_library(erhe::renderer ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES

    camera_buffer.cpp
    camera_buffer.hpp
    draw_indirect_buffer.cpp
    draw_indirect_buffer.hpp
    forward_renderer.cpp
    forward_renderer.hpp
    joint_buffer.cpp
    joint_buffer.hpp
    light_buffer.cpp
    light_buffer.hpp
    material_buffer.cpp
    material_buffer.hpp
    pipeline_renderpass.cpp
    pipeline_renderpass.hpp
    primitive_buffer.cpp
    primitive_buffer.hpp
    program_interface.cpp
    program_interface.hpp
    shadow_renderer.cpp
    shadow_renderer.hpp


    renderer_log.cpp
    renderer_log.hpp
    #renderer_message.hpp
    #renderer_message_bus.cpp
    #renderer_message_bus.hpp
)

target_link_libraries(
    ${_target}
    PRIVATE
    erhe::application # TODO move used features to somewhere else
    erhe::components
    erhe::concurrency
    erhe::gl
    erhe::graphics
    erhe::log
    erhe::primitive
    erhe::scene
    erhe::toolkit
)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
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
