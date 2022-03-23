# Jolt Physics has CMakeLists.txt in Build subdirectory

function (FetchContent_MakeAvailable_JoltPhysics)
    FetchContent_GetProperties(JoltPhysics)
    string(TOLOWER "JoltPhysics" lc_JoltPhysics)
    if (NOT ${lc_JoltPhysics}_POPULATED)
        FetchContent_Populate(JoltPhysics)

        option(TARGET_UNIT_TESTS "Build Unit Tests" OFF)
        option(TARGET_HELLO_WORLD "Build Hello World" OFF)
        option(TARGET_PERFORMANCE_TEST "Build Performance Test" OFF)
        option(TARGET_SAMPLES "Build Samples" OFF)
        option(TARGET_VIEWER "Build JoltViewer" OFF)

        option(USE_SSE4_1 "Enable SSE4.1" OFF)
        option(USE_SSE4_2 "Enable SSE4.2" ON)
        option(USE_AVX "Enable AVX" OFF)
        option(USE_AVX2 "Enable AVX2" OFF)
        option(USE_LZCNT "Enable LZCNT" ON)
        option(USE_TZCNT "Enable TZCNT" ON)
        option(USE_F16C "Enable F16C" ON)
        option(USE_FMADD "Enable FMADD" ON)

        add_definitions(-DJPH_FLOATING_POINT_EXCEPTIONS_ENABLED)

        add_subdirectory(${${lc_JoltPhysics}_SOURCE_DIR}/Build ${${lc_JoltPhysics}_BINARY_DIR})
    endif ()
endfunction ()
