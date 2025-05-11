# Use latest greatest C++ version
add_definitions(-DGLM_FORCE_CXX2A=1)

# Use improved precision depth range
add_definitions(-DGLM_FORCE_DEPTH_ZERO_TO_ONE)
add_definitions(-DFMT_HEADER_ONLY)
add_definitions(-DERHE_DLOAD_ALL_GL_SYMBOLS)

find_package(Threads REQUIRED)

function (erhe_target_settings target foldername)
    erhe_target_settings_toolchain(${target})

    if(NOT "${target}" STREQUAL "erhe_pch")
        if (${ERHE_USE_PRECOMPILED_HEADERS})
            target_precompile_headers(${target} REUSE_FROM erhe_pch)

            # Avoid "POSIX thread support was disabled in AST file"
            target_link_libraries(${target} PRIVATE Threads::Threads)

            # Distribute compile defines defines from spdlog
            target_link_libraries(${target} PUBLIC erhe_pch)
        endif ()
    endif ()

    set_property(TARGET ${target} PROPERTY FOLDER ${foldername})

endfunction()
