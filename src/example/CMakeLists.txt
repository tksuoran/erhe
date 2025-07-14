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
    res/shaders/depth.vert
    res/shaders/depth.frag
    res/shaders/standard.vert
    res/shaders/standard.frag
    erhe.ini
)
target_link_libraries(
    ${_target}
    PRIVATE
    erhe::utility
    erhe::dataformat
    erhe::file
    erhe::gl
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
    mINI
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
