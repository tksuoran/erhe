set(_target erhe_rendergraph)
add_library(${_target})
add_library(erhe::rendergraph ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    multisample_resolve.cpp
    multisample_resolve.hpp
    rendergraph.cpp
    rendergraph.hpp
    rendergraph_log.cpp
    rendergraph_log.hpp
    rendergraph_node.cpp
    rendergraph_node.hpp
    resource_routing.cpp
    resource_routing.hpp
    sink_rendergraph_node.cpp
    sink_rendergraph_node.hpp
    texture_rendergraph_node.cpp
    texture_rendergraph_node.hpp
)

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

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
