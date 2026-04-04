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
    example_mesh_memory.json
    logging.json
)

# Required for log init
if (ERHE_GRAPHICS_API_OPENGL)
    target_link_libraries(${_target} PRIVATE erhe::gl)
endif ()

if (ERHE_USE_MIMALLOC)
    target_link_libraries(${_target} PUBLIC mimalloc-static)
endif ()

target_link_libraries(
    ${_target}
    PRIVATE
        Taskflow
        erhe::codegen
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

# Codegen for example config
set(_example_defs "${CMAKE_CURRENT_SOURCE_DIR}/definitions")
set(_example_gen  "${CMAKE_CURRENT_BINARY_DIR}/generated")

erhe_codegen_generate(
    TARGET          ${_target}
    DEFINITIONS_DIR "${_example_defs}"
    OUTPUT_DIR      "${_example_gen}"
    DEFINITIONS
        "${_example_defs}/example_mesh_memory.py"
)

set(_gfx_gen "${CMAKE_BINARY_DIR}/src/erhe/graphics/erhe_graphics/generated")
set(_example_codegen_sources
    ${_example_gen}/example_mesh_memory.hpp
    ${_example_gen}/example_mesh_memory_serialization.hpp
    ${_example_gen}/example_mesh_memory.cpp
    ${_gfx_gen}/graphics_config_serialization.hpp
    ${_gfx_gen}/graphics_config.cpp
)
set_source_files_properties(${_example_codegen_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_example_codegen_sources})
source_group("codegen generated" FILES ${_example_codegen_sources})

target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD                  20
    CXX_STANDARD_REQUIRED         YES
    CXX_EXTENSIONS                NO
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEBUGGER_WORKING_DIRECTORY    "${CMAKE_CURRENT_SOURCE_DIR}"
)

erhe_target_settings(${_target} "erhe-executables")
