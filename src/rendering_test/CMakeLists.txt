message(STATUS "Configuring executable 'rendering_test'.")
set(_target "rendering_test")
add_executable(${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    main.cpp
    mesh_memory.cpp
    mesh_memory.hpp
    programs.cpp
    programs.hpp
    rendering_test.cpp
    rendering_test.hpp
    rendering_test_log.cpp
    rendering_test_log.hpp
    res/shaders/standard.vert
    res/shaders/standard.frag
    res/shaders/textured_quad.vert
    res/shaders/textured_quad.frag
    res/shaders/multi_texture_test.vert
    res/shaders/multi_texture_test.frag
    res/shaders/separate_samplers_test.frag
    res/shaders/wide_lines.vert
    res/shaders/wide_lines.geom
    res/shaders/wide_lines.frag
    res/shaders/compute_before_content_line.comp
    res/shaders/line_after_compute.vert
    res/shaders/line_after_compute.frag
    erhe.json
    logging.json
    rendering_test_mesh_memory.json
)

# Codegen for rendering test config
set(_defs "${CMAKE_CURRENT_SOURCE_DIR}/definitions")
set(_gen  "${CMAKE_CURRENT_BINARY_DIR}/generated")

erhe_codegen_generate(
    TARGET          ${_target}
    DEFINITIONS_DIR "${_defs}"
    OUTPUT_DIR      "${_gen}"
    DEFINITIONS
        "${_defs}/rendering_test_mesh_memory.py"
)

set(_codegen_sources
    ${_gen}/rendering_test_mesh_memory.hpp
    ${_gen}/rendering_test_mesh_memory_serialization.hpp
    ${_gen}/rendering_test_mesh_memory.cpp
)
set_source_files_properties(${_codegen_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_codegen_sources})
source_group("codegen generated" FILES ${_codegen_sources})

target_include_directories(${_target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
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
        simdjson
        erhe::codegen
        erhe::utility
        erhe::dataformat
        erhe::file
        erhe::geometry
        erhe::gltf
        erhe::graphics
        erhe::graphics_buffer_sink
        erhe::log
        erhe::math
        erhe::primitive
        erhe::profile
        erhe::raytrace
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

set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD                  20
    CXX_STANDARD_REQUIRED         YES
    CXX_EXTENSIONS                NO
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEBUGGER_WORKING_DIRECTORY    "${CMAKE_CURRENT_SOURCE_DIR}"
)

erhe_target_settings(${_target} "erhe-executables")
