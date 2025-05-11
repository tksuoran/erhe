set(_target "erhe_item")
add_library(${_target})
add_library(erhe::item ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_item/constexpr-xxh3.h
    erhe_item/hierarchy.cpp
    erhe_item/hierarchy.hpp
    erhe_item/item.cpp
    erhe_item/item.hpp
    erhe_item/item_host.cpp
    erhe_item/item_host.hpp
    erhe_item/item_log.cpp
    erhe_item/item_log.hpp
    erhe_item/unique_id.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
    target_link_libraries(${_target} PUBLIC erhe_pch)
endif ()

target_link_libraries(
    ${_target}
    PUBLIC
        erhe::profile
        glm::glm-header-only
    PRIVATE
        erhe::bit
        erhe::log
        erhe::message_bus
        erhe::verify
        fmt::fmt
)
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
