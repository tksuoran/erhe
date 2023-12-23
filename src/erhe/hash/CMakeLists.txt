set(_target "erhe_hash")
add_library(${_target})
add_library(erhe::hash ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_hash/hash.cpp
    erhe_hash/hash.hpp
    erhe_hash/xxhash.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${_target} PUBLIC glm::glm-header-only)
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
