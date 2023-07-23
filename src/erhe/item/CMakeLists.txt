set(_target "erhe_item")
add_library(${_target})
add_library(erhe::item ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    constexpr-xxh3.h
    hierarchy.cpp
    hierarchy.hpp
    item.cpp
    item.hpp
    item_host.cpp
    item_host.hpp
    item_log.cpp
    item_log.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(
    ${_target}
    PRIVATE
        erhe::log
        erhe::message_bus
        fmt::fmt
    PUBLIC
        glm::glm
)

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
