# Jolt Physics has CMakeLists.txt in Build subdirectory

function (FetchContent_MakeAvailable_JoltPhysics)
    FetchContent_GetProperties(JoltPhysics)
    string(TOLOWER "JoltPhysics" lc_JoltPhysics)
    if (NOT ${lc_JoltPhysics}_POPULATED)
        FetchContent_Populate(JoltPhysics)

        option(DOUBLE_PRECISION "Use double precision math" OFF)
        option(GENERATE_DEBUG_SYMBOLS "Generate debug symbols" ON)
        option(CROSS_PLATFORM_DETERMINISTIC "Cross platform deterministic" OFF)
        option(CROSS_COMPILE_ARM "Cross compile to aarch64-linux-gnu" OFF)
        option(BUILD_SHARED_LIBS "Compile Jolt as a shared library" OFF)
        option(INTERPROCEDURAL_OPTIMIZATION "Enable interprocedural optimizations" OFF) # Differs from Jolt
        option(FLOATING_POINT_EXCEPTIONS_ENABLED "Enable floating point exceptions" ON)
        option(OBJECT_LAYER_BITS "Number of bits in ObjectLayer" 16)
        option(USE_SSE4_1 "Enable SSE4.1" ON)
        option(USE_SSE4_2 "Enable SSE4.2" ON)
        option(USE_AVX "Enable AVX" ON)
        option(USE_AVX2 "Enable AVX2" ON)
        option(USE_AVX512 "Enable AVX512" OFF)
        option(USE_LZCNT "Enable LZCNT" ON)
        option(USE_TZCNT "Enable TZCNT" ON)
        option(USE_F16C "Enable F16C" ON)
        option(USE_FMADD "Enable FMADD" ON)
        option(ENABLE_ALL_WARNINGS "Enable all warnings and warnings as errors" ON)
        option(USE_STATIC_MSVC_RUNTIME_LIBRARY "Use the static MSVC runtime library" OFF)
        
        add_definitions(-DJPH_FLOATING_POINT_EXCEPTIONS_ENABLED)

        add_subdirectory(${${lc_JoltPhysics}_SOURCE_DIR}/Build ${${lc_JoltPhysics}_BINARY_DIR})
    endif ()
endfunction ()
