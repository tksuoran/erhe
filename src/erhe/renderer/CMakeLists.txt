set(_target "erhe_renderer")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_library(${_target})
add_library(erhe::renderer ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES

    buffer_writer.cpp
    buffer_writer.hpp
    draw_indirect_buffer.cpp
    draw_indirect_buffer.hpp
    line_renderer.cpp
    line_renderer.hpp
    multi_buffer.cpp
    multi_buffer.hpp
    pipeline_renderpass.cpp
    pipeline_renderpass.hpp
    renderer_log.cpp
    renderer_log.hpp
    text_renderer.cpp
    text_renderer.hpp
)

target_link_libraries(
    ${_target}
    PRIVATE
        erhe::concurrency
        erhe::gl
        erhe::graphics
        erhe::log
        #erhe::primitive
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
