# Jolt Physics has CMakeLists.txt in Build subdirectory

add_compile_definitions(SPDLOG_FMT_EXTERNAL)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
    message("Applying Jolt compatible compiler settings for Windows")
    set(CLANG_LIB_PATH "\"$(VSInstallDir)\\VC\\Tools\\Llvm\\x64\\lib\\clang\\${CMAKE_CXX_COMPILER_VERSION}\\lib\\windows\"")
    set(CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE "x64")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # Jolt has /GR-  disable runtime type information
    #          /WX   treat warnings as errors
    # Zi      separate pdb
    # Gm      minimal rebuild
    # FC      Full path of source code file in diagnostics
    # GS      buffer security check
    # GL      whole program optimization
    # Gy      enable function level linking
    # Od      disable all optimizations, faster to compile
    # Ob0     disable inline expansion
    # Oi      generate intrinsic functions
    # Ot      favor fast code
    # MP      build with multiple processes
    # fp:fast fast floating-point model; results are less predicable
    # /DUNICODE /D_UNICODE
    #
    # EHs             Enables standard C++ stack unwinding.
    #                 Catches only standard C++ exceptions when you use catch(...) syntax.
    #                 Unless /EHc is also specified, the compiler assumes that functions declared as extern "C" may throw a C++ exception.
    # EHc             When used with /EHs, the compiler assumes that functions declared as extern "C" never throw a C++ exception.
    #                 It has no effect when used with /EHa (that is, /EHca is equivalent to /EHa).
    #                 /EHc is ignored if /EHs or /EHa aren't specified.
    # /Zc:inline      Remove unreferenced COMDAT
    # /Zc:__cplusplus Report updated __cplusplus
    # FC              Full path of source code file in diagnostics
    #set(CMAKE_CXX_FLAGS "/std:c++17 /Zc:__cplusplus /Gm- /EHsc /nologo /diagnostics:classic /FC /fp:except- /Zc:inline /Zi /DWIN32 /D_WINDOWS")
    # Removed /Gm- because it is deprecaed.
    # Removed /GS because it is on by default
    # Removed /nologo
    # Removed /diagnostics:classic 
    set(CMAKE_CXX_FLAGS         "/std:c++17 /Zc:__cplusplus /EHsc /FC /fp:except- /Zc:inline /Zi /DWIN32 /D_WINDOWS")
    set(CMAKE_CXX_FLAGS_DEBUG   "/Zi /Ob0 /Od /RTC1 /D_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/DNDEBUG /GS- /GL /Gy /O2 /Oi /Ot")
    #### set(CMAKE_CXX_FLAGS_DISTRIBUTION "/DNDEBUG /GS- /GL /Gy /O2 /Oi /Ot")
    #### set(CMAKE_CXX_FLAGS_RELEASEASAN "/DNDEBUG -fsanitize=address /Od")
    #### set(CMAKE_CXX_FLAGS_RELEASEUBSAN "/DNDEBUG -fsanitize=undefined,implicit-conversion")
    #### set(CMAKE_CXX_FLAGS_RELEASECOVERAGE "/DNDEBUG -fprofile-instr-generate -fcoverage-mapping")
    # Jolt has /SUBSYSTEM:WINDOWS
    set(CMAKE_EXE_LINKER_FLAGS "/machine:x64 /ignore:4221 /DEBUG:FASTLINK")
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /fp:fast") # Clang doesn't use fast math because it cannot be turned off inside a single compilation unit
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}") # Clang turns Float2 into a vector sometimes causing floating point exceptions
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO /LTCG:incremental /OPT:ICF /OPT:REF")
        set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "/LTCG")
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /showFilenames")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2 -mpopcnt")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mlzcnt")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mbmi")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mf16c")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfma")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASEASAN "/SUBSYSTEM:CONSOLE /LIBPATH:${CLANG_LIB_PATH} clang_rt.asan-x86_64.lib -wholearchive:clang_rt.asan-x86_64.lib clang_rt.asan_cxx-x86_64.lib -wholearchive:clang_rt.asan_cxx-x86_64.lib")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASEUBSAN "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LIBPATH:${CLANG_LIB_PATH}")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASECOVERAGE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LIBPATH:${CLANG_LIB_PATH}")
    endif()
elseif ("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin" OR "${CMAKE_SYSTEM_NAME}" STREQUAL "iOS")
    message("Applying Jolt compatible compiler settings for Linux")
    #set(CMAKE_CXX_FLAGS "-g -std=c++17 -I. -Wall -Werror")
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        message("Applying Jolt compatible compiler settings for GCC")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-comment -ffp-contract=off -Wno-inline")
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
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")
endif()

# Set linker flags
set(CMAKE_EXE_LINKER_FLAGS_DISTRIBUTION "${CMAKE_EXE_LINKER_FLAGS_RELEASE}")
