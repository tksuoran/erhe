function (FetchContent_MakeAvailable_glslang)
    FetchContent_GetProperties(glslang)
    string(TOLOWER "glslang" lc_glslang)
    if (NOT ${lc_glslang}_POPULATED)
        FetchContent_Populate(glslang)

        # Apply custom configuration
        option(GLSLANG_TESTS           "Enable glslang testing"          OFF)
        option(GLSLANG_ENABLE_INSTALL  "Enable glslang installation"     OFF)
        option(ENABLE_SPIRV            "Enables SPIRV output support"    ON)
        option(ENABLE_SPVREMAPPER      "Enables building of SPVRemapper" OFF)
        option(ENABLE_GLSLANG_BINARIES "Builds glslang and spirv-remap"  OFF)
        option(ENABLE_GLSLANG_JS       "If using Emscripten, build glslang.js. Otherwise, builds a sample executable for binary-size testing." OFF)
        option(ENABLE_HLSL             "Enables HLSL input support"      OFF)
        option(ENABLE_RTTI             "Enables RTTI"                    ON)
        option(ENABLE_EXCEPTIONS       "Enables Exceptions"              ON)
        option(ENABLE_OPT              "Enables spirv-opt capability if present" OFF) # TODO enable, requires spirv tools?
        option(ENABLE_PCH              "Enables Precompiled header" ${ERHE_USE_PRECOMPILED_HEADERS})

        add_subdirectory(${${lc_glslang}_SOURCE_DIR} ${${lc_glslang}_BINARY_DIR})
    endif ()
endfunction ()
