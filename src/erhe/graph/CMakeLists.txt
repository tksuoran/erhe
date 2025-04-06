set(_target "erhe_graph")
add_library(${_target})
add_library(erhe::graph ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_graph/pin.cpp
    erhe_graph/pin.hpp
    erhe_graph/link.cpp
    erhe_graph/link.hpp
    erhe_graph/node.cpp
    erhe_graph/node.hpp
    erhe_graph/graph.cpp
    erhe_graph/graph.hpp
    erhe_graph/graph_log.cpp
    erhe_graph/graph_log.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
erhe_target_settings(${_target})
target_link_libraries(${_target}
    PRIVATE
        erhe::defer
        erhe::log
        erhe::verify
        erhe::item
)
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
