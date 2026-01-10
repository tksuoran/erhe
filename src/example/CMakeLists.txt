message(STATUS "Configuring executable 'example'.")
set(_target "example")
add_executable(${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    example.cpp
    example.hpp
    example_log.cpp
    example_log.hpp
    frame_controller.cpp
    frame_controller.hpp
    main.cpp
    mesh_memory.cpp
    mesh_memory.hpp
    programs.cpp
    programs.hpp
    res/models/Box.gltf
    res/shaders/erhe_bxdf.glsl
    res/shaders/erhe_ggx.glsl
    res/shaders/erhe_light.glsl
    res/shaders/erhe_math.glsl
    res/shaders/erhe_srgb.glsl
    res/shaders/erhe_texture.glsl
    res/shaders/erhe_tonemap.glsl
    res/shaders/depth.vert
    res/shaders/depth.frag
    res/shaders/standard.vert
    res/shaders/standard.frag
    erhe.toml
    logging.toml
)

# Required for log init
if (ERHE_GRAPHICS_API_OPENGL)
    target_link_libraries(${_target} PRIVATE erhe::gl)
endif ()

target_link_libraries(
    ${_target}
    PRIVATE
    Taskflow
    erhe::utility
    erhe::dataformat
    erhe::file
    erhe::gltf
    erhe::graphics
    erhe::graphics_buffer_sink
    erhe::log
    erhe::math
    erhe::primitive
    erhe::profile
    erhe::raytrace # TODO Make it optional - glTF loader currently uses it and log needs to be initialized..
    erhe::renderer
    erhe::scene
    erhe::scene_renderer
    erhe::ui
    erhe::verify
    erhe::window
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

erhe_target_settings(${_target} "erhe-executables")
