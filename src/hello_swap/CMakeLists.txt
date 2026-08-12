message(STATUS "Configuring executable 'hello_swap'.")
set(_target "hello_swap")
add_executable(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${PROJECT_SOURCE_DIR}" FILES
    config/hello_swap/erhe_graphics.json
    config/hello_swap/logging.json
    config/hello_swap/window.json
)

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    hello_swap.cpp
    hello_swap.hpp
    hello_swap_log.cpp
    hello_swap_log.hpp
    main.cpp
)

# Graphics_config serialize/deserialize must compile here (same target as
# other simdjson users) to avoid ABI mismatch from differing arch flags.
set(_gfx_gen "${CMAKE_BINARY_DIR}/src/erhe/graphics/erhe_graphics/generated")
set(_gfx_ser_sources
    ${_gfx_gen}/graphics_config_serialization.hpp
    ${_gfx_gen}/graphics_config.cpp
    ${_gfx_gen}/opengl_config_serialization.hpp
    ${_gfx_gen}/opengl_config.cpp
    ${_gfx_gen}/vulkan_config_serialization.hpp
    ${_gfx_gen}/vulkan_config.cpp
    ${_gfx_gen}/metal_config_serialization.hpp
    ${_gfx_gen}/metal_config.cpp
)
erhe_codegen_source_settings(${_gfx_ser_sources})
target_sources(${_target} PRIVATE ${_gfx_ser_sources})
source_group("codegen generated/graphics" FILES ${_gfx_ser_sources})

# Required for log init
if (ERHE_GRAPHICS_API_OPENGL)
    target_link_libraries(${_target} PRIVATE erhe::gl)
endif ()

if (ERHE_USE_MIMALLOC)
    target_link_libraries(${_target} PUBLIC mimalloc-static)
endif ()

target_link_libraries(
    ${_target}
    PRIVATE
        erhe::codegen
        erhe::file
        erhe::graphics
        erhe::log
        erhe::math
        erhe::utility
        erhe::verify
        erhe::window
)

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS        NO
)

erhe_target_settings(${_target} "erhe-executables")
