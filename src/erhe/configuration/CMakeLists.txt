set(_target erhe_configuration)
add_library(${_target})
add_library(erhe::configuration ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_configuration/configuration.cpp
    erhe_configuration/configuration.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        erhe::profile
        glm::glm-header-only
        tomlplusplus::tomlplusplus
    PRIVATE
        etl::etl
        erhe::file
)

erhe_target_settings(${_target} "erhe")

