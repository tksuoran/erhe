# Jolt Physics has CMakeLists.txt in Build subdirectory

add_compile_definitions(SPDLOG_FMT_EXTERNAL)
set(CROSS_PLATFORM_DETERMINISTIC ON)
if (MSVC)
    message("Applying Jolt compatible compiler settings for MSVC")
    set(CLANG_LIB_PATH "\"$(VSInstallDir)\\VC\\Tools\\Llvm\\x64\\lib\\clang\\${CMAKE_CXX_COMPILER_VERSION}\\lib\\windows\"")
    set(CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE "x64")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # Set general compiler flags
    # erhe: Removed /Gm- because it is deprecaed.
    # erhe: Removed /nologo
    # erhe: Removed /diagnostics:classic 
    # erhe: Removed /fp:except- (for now)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus /MP /FC /Zc:inline")

    # Set compiler flag for enabling RTTI
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR")

    # Enable exceptions
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")

    # Set compiler flags for various configurations
    # erhe: Added debug symbols
    set(CMAKE_CXX_FLAGS_DEBUG   "/GS /Od /Ob0 /RTC1 /Zi")
    set(CMAKE_CXX_FLAGS_RELEASE "/GS- /Gy /O2 /Oi /Ot")

    # Jolt has /SUBSYSTEM:WINDOWS
    set(CMAKE_EXE_LINKER_FLAGS "/ignore:4221")

    # Set linker flags
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
        if (CROSS_PLATFORM_DETERMINISTIC)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:precise")
        else()
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:fast") # Clang doesn't use fast math because it cannot be turned off inside a single compilation unit
        endif()
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /showFilenames")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Qunused-arguments") # Clang emits warnings about unused arguments such as /MP and /GL
        set(CMAKE_EXE_LINKER_FLAGS_RELEASEASAN "/SUBSYSTEM:CONSOLE /LIBPATH:${CLANG_LIB_PATH} clang_rt.asan-x86_64.lib -wholearchive:clang_rt.asan-x86_64.lib clang_rt.asan_cxx-x86_64.lib -wholearchive:clang_rt.asan_cxx-x86_64.lib")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASEUBSAN "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LIBPATH:${CLANG_LIB_PATH}")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASECOVERAGE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LIBPATH:${CLANG_LIB_PATH}")
    endif()
else ()
    message("Applying Jolt compatible compiler settings for non-MSVC")

    #### This is older configuration - it does NOT cause std::vector<> desctructor to hang

    #set(CMAKE_CXX_FLAGS "-g -std=c++17 -I. -Wall -Werror")
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        message("Applying Jolt compatible compiler settings for GCC")
        #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-comment -ffp-contract=off -Wno-inline")
    endif()
    if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64" OR "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "AMD64")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2 -mpopcnt")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mlzcnt")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mbmi")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mf16c")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfma")
    #elseif ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "aarch64")
    #	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    endif()
    set(CMAKE_CXX_FLAGS_DEBUG "-D_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_DISTRIBUTION "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASEASAN "-DNDEBUG -fsanitize=address")
    set(CMAKE_CXX_FLAGS_RELEASEUBSAN "-DNDEBUG -fsanitize=undefined,implicit-conversion")
    set(CMAKE_CXX_FLAGS_RELEASECOVERAGE "-DNDEBUG -fprofile-instr-generate -fcoverage-mapping")    

    #### This would be more up to date configuration from Jolt. Unfortunately, using these
    #### settings causes msys2 clang64 build to hang during startup in std::vector<> desctructor.
    #### TODO investigate

    # Enable warnings
    #### if (ENABLE_ALL_WARNINGS)
    ####     set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror")
    #### endif()
    #### 
    #### # Optionally generate debug symbols
    #### if (GENERATE_DEBUG_SYMBOLS)
    ####     set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
    #### endif()

    #### if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    ####     # Also disable -Wstringop-overflow or it will generate false positives that can't be disabled from code when link-time optimizations are enabled
    ####     # Also turn off automatic fused multiply add contractions, there doesn't seem to be a way to do this selectively through the macro JPH_PRECISE_MATH_OFF
    ####     set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-stringop-overflow -ffp-contract=off")
    #### else()
    ####     # Do not use -ffast-math since it cannot be turned off in a single compilation unit under clang, see Core.h
    ####     set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffp-model=precise")
    #### 
    ####     # On clang 14 and later we can turn off float contraction through a pragma, older versions and deterministic versions need it off always, see Core.h
    ####     if (CMAKE_CXX_COMPILER_VERSION LESS 14 OR CROSS_PLATFORM_DETERMINISTIC)
    ####         set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffp-contract=off")
    ####     endif()
    #### endif()

    #### # See https://github.com/jrouwe/JoltPhysics/issues/922. When compiling with DOUBLE_PRECISION=YES and CMAKE_OSX_DEPLOYMENT_TARGET=10.12 clang triggers a warning that we silence here.
    #### if ("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin" AND "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    ####     set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -faligned-allocation")
    #### endif()

    #### # Cross compiler flags
    #### if (CROSS_COMPILE_ARM)
    ####     set(CMAKE_CXX_FLAGS "--target=aarch64-linux-gnu ${CMAKE_CXX_FLAGS}")
    #### endif()
    #### 
    #### # Set compiler flags for various configurations
    #### if (OVERRIDE_CXX_FLAGS)
    ####     set(CMAKE_CXX_FLAGS_DEBUG "")
    ####     set(CMAKE_CXX_FLAGS_RELEASE "-O3")
    #### endif()
    #### set(CMAKE_CXX_FLAGS_DISTRIBUTION "${CMAKE_CXX_FLAGS_RELEASE}")
    #### set(CMAKE_CXX_FLAGS_RELEASEASAN "-fsanitize=address")
    #### set(CMAKE_CXX_FLAGS_RELEASEUBSAN "-fsanitize=undefined,implicit-conversion,float-divide-by-zero,local-bounds -fno-sanitize-recover=all")
    #### set(CMAKE_CXX_FLAGS_RELEASECOVERAGE "-O0 -DJPH_NO_FORCE_INLINE -fprofile-instr-generate -fcoverage-mapping")
    #### 
    #### # Set linker flags
    #### if (NOT ("${CMAKE_SYSTEM_NAME}" STREQUAL "Windows"))
    ####     set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")
    #### endif()
endif()

# Set linker flags
set(CMAKE_EXE_LINKER_FLAGS_DISTRIBUTION "${CMAKE_EXE_LINKER_FLAGS_RELEASE}")
    