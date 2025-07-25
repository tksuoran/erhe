set(_target erhe_rendergraph)
add_library(${_target})
add_library(erhe::rendergraph ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_rendergraph/rendergraph.cpp
    erhe_rendergraph/rendergraph.hpp
    erhe_rendergraph/rendergraph_log.cpp
    erhe_rendergraph/rendergraph_log.hpp
    erhe_rendergraph/rendergraph_node.cpp
    erhe_rendergraph/rendergraph_node.hpp
    erhe_rendergraph/resource_routing.cpp
    erhe_rendergraph/resource_routing.hpp
    erhe_rendergraph/sink_rendergraph_node.cpp
    erhe_rendergraph/sink_rendergraph_node.hpp
    erhe_rendergraph/texture_rendergraph_node.cpp
    erhe_rendergraph/texture_rendergraph_node.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        fmt::fmt
        erhe::graphics
        erhe::log
        erhe::math
        erhe::ui
    PRIVATE
        erhe::profile
        erhe::verify
)

erhe_target_settings(${_target} "erhe")
