set(_target "erhe_property")
add_library(${_target})
add_library(erhe::property ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_property/attached_group.cpp
    erhe_property/attached_group.hpp
    erhe_property/dependency_object.cpp
    erhe_property/dependency_object.hpp
    erhe_property/dependency_property.cpp
    erhe_property/dependency_property.hpp
    erhe_property/enum_info.cpp
    erhe_property/enum_info.hpp
    erhe_property/expression.cpp
    erhe_property/expression.hpp
    erhe_property/owner_type.hpp
    erhe_property/property_log.cpp
    erhe_property/property_log.hpp
    erhe_property/property_metadata.hpp
    erhe_property/property_set.cpp
    erhe_property/property_set.hpp
    erhe_property/property_string.cpp
    erhe_property/property_style.hpp
    erhe_property/property_string.hpp
    erhe_property/property_value.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    ${_target}
    PUBLIC
        erhe::profile
        erhe::utility
        glm::glm-header-only
    PRIVATE
        erhe::log
        erhe::verify
        fmt::fmt
        tinyexpr
)

erhe_target_settings(${_target} "erhe")

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    add_subdirectory(test)
endif ()
