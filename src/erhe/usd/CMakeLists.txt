set(_target "erhe_usd")
add_library(${_target})
add_library(erhe::usd ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_usd/usd.cpp
    erhe_usd/usd.hpp
    erhe_usd/usd_impl.hpp
    erhe_usd/usd_import.cpp
    erhe_usd/usd_log.cpp
    erhe_usd/usd_log.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# LightUSD headers are included by erhe_usd/usd.cpp only, and they are not
# clean at erhe's warning level, so the directory is a SYSTEM include here.
# LightUSD's own static target keeps this directory PRIVATE, which is why it
# is named explicitly rather than picked up from the link interface.
target_include_directories(${_target} SYSTEM PRIVATE ${ERHE_LIGHTUSD_INCLUDE_DIR})

target_link_libraries(${_target}
    PRIVATE
        erhe::dataformat
        erhe::geometry
        erhe::item
        erhe::log
        erhe::primitive
        erhe::profile
        erhe::scene
        erhe::verify
        fmt::fmt
        lightusd::lightusd_static
)

erhe_target_settings(${_target} "erhe")

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    add_subdirectory(test)
endif ()
