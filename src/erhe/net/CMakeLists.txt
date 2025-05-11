set(_target "erhe_net")
add_library(${_target})
add_library(erhe::net ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_net/client.cpp
    erhe_net/client.hpp
    erhe_net/net_log.cpp
    erhe_net/net_log.hpp
    erhe_net/net_common.cpp
    erhe_net/net_os.hpp
    erhe_net/ring_buffer.cpp
    erhe_net/ring_buffer.hpp
    erhe_net/select_sockets.cpp
    erhe_net/select_sockets.hpp
    erhe_net/server.cpp
    erhe_net/server.hpp
    erhe_net/socket.cpp
    erhe_net/socket.hpp
)

if (ERHE_TARGET_OS_WINDOWS)
    target_link_libraries(${_target} PUBLIC ws2_32.lib)
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}/erhe_net" FILES
        erhe_net/net_windows.cpp
    )
endif ()

if (ERHE_TARGET_OS_LINUX)
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}/erhe_net" FILES
        erhe_net/net_linux.cpp
    )
endif ()

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
    target_link_libraries(${_target} PUBLIC erhe_pch)
endif ()

target_link_libraries(${_target}
    PUBLIC
        erhe::log
        erhe::verify
        fmt::fmt
)

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
