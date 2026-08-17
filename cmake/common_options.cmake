# Use latest greatest C++ version
add_definitions(-DGLM_FORCE_CXX2A=1)

# Use improved precision depth range
add_definitions(-DGLM_FORCE_DEPTH_ZERO_TO_ONE)

# Require explicit conversions between glm types. Without this, mat4 converts to
# quat implicitly, so overload resolution can silently pick a quaternion overload
# and run the matrix through quat_cast - which does not project a non-orthonormal
# matrix onto the nearest rotation. That is how the editor camera basis was able
# to accumulate scale until the view rolled by tens of degrees.
add_definitions(-DGLM_FORCE_EXPLICIT_CTOR)
add_definitions(-DFMT_HEADER_ONLY)
add_definitions(-DERHE_DLOAD_ALL_GL_SYMBOLS)

function (erhe_target_settings target foldername)
    erhe_target_settings_toolchain(${target})

    if(NOT "${target}" STREQUAL "erhe_pch")
        if (${ERHE_USE_PRECOMPILED_HEADERS})
            target_precompile_headers(${target} REUSE_FROM erhe_pch)

            # Distribute compile defines defines from spdlog
            target_link_libraries(${target} PUBLIC erhe_pch)
        endif ()
    endif ()

    set_property(TARGET ${target} PROPERTY FOLDER ${foldername})

endfunction()
