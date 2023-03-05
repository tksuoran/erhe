set(_target "erhe_net")
add_library(${_target})
add_library(erhe::net ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    client.cpp
    client.hpp
    net_log.cpp
    net_log.hpp
    net_common.cpp
    net_os.hpp
    ring_buffer.cpp
    ring_buffer.hpp
    select_sockets.cpp
    select_sockets.hpp
    server.cpp
    server.hpp
    socket.cpp
    socket.hpp
)

if (ERHE_TARGET_OS_WINDOWS)
    target_link_libraries(${_target} PUBLIC ws2_32.lib)
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        net_windows.cpp
    )
endif ()

if (ERHE_TARGET_OS_LINUX)
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        net_linux.cpp
    )
endif ()

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        #spdlog::spdlog
        spdlog::spdlog_header_only
        fmt::fmt
)

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
