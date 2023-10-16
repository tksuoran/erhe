function (FetchContent_MakeAvailable_glslang)
    FetchContent_GetProperties(glslang)
    string(TOLOWER "glslang" lc_glslang)
    if (NOT ${lc_glslang}_POPULATED)
        FetchContent_Populate(glslang)

        # Apply custom configuration
        option(SKIP_GLSLANG_INSTALL    "Skip installation"                       OFF)
        option(ENABLE_SPVREMAPPER      "Enables building of SPVRemapper"         OFF)
        option(ENABLE_GLSLANG_BINARIES "Builds glslang and spirv-remap"          OFF)
        option(ENABLE_HLSL             "Enables HLSL input support"              OFF)
        option(ENABLE_RTTI             "Enables RTTI"                            ON)
        option(ENABLE_EXCEPTIONS       "Enables Exceptions"                      ON)
        option(ENABLE_OPT              "Enables spirv-opt capability if present" ON)
        option(ENABLE_CTEST            "Enables testing"                         OFF)

        add_subdirectory(${${lc_glslang}_SOURCE_DIR} ${${lc_glslang}_BINARY_DIR})
    endif ()
endfunction ()
