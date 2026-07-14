# CMakeLists.txt for erhe/src

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS        NO)

if (MSVC)
    # CMake injects "-external:W0" next to the "-external:I" flags it emits
    # for SYSTEM include directories (Modules/Compiler/MSVC.cmake). Every
    # erhe target then appends /external:W3 (erhe_target_settings_toolchain
    # in cmake/msvc.cmake), which overrides the W0 and makes cl print
    # "D9025: overriding '/external:W0' with '/external:W3'" once per TU.
    # Suppress the injection for this source tree so /external:W3 is the
    # single external warning level here. The CPM dependencies are added
    # from the root scope and keep CMake's default W0; the vendored
    # third-party under src/ (imgui, mango, ...) receives no SYSTEM
    # includes, so this is a no-op for it.
    set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX_WARNING "")
endif ()

if (WIN32)
    # Dependency targets export Windows-version/charset macros PUBLIC to
    # their consumers (mango: UNICODE; Tracy under clang: WINVER=0x0A00 +
    # _WIN32_WINNT=0x0A00), so most -- but not all -- targets in this tree
    # compiled with them, while erhe_pch (whose erhe_windows.hpp includes
    # <windows.h>, i.e. sdkddkver.h) did not. clang-cl's PCH validation
    # rightly flags that divergence (-Wclang-cl-pch / -Wmacro-redefined).
    # erhe is not a TCHAR codebase and targets Windows 10+; define the
    # macros uniformly for the whole src/ tree (including the PCH) so every
    # TU preprocesses windows.h the same way.
    add_compile_definitions(UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
endif ()

#add_subdirectory(hidapi)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    message(STATUS "Adding subdirectory 'superluminal'.")
    add_subdirectory(superluminal)
endif ()

if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
    message(STATUS "Adding subdirectory 'imgui'.")
    add_subdirectory(imgui)
    message(STATUS "Adding subdirectory 'imgui_gradient'.")
    add_subdirectory(imgui_gradient)
endif ()

if (${ERHE_AUDIO_LIBRARY} STREQUAL "miniaudio")
    message(STATUS "Adding subdirectory 'miniaudio'.")
    add_subdirectory(miniaudio)
endif ()

add_subdirectory(rapidjson)
add_subdirectory(RectangleBinPack)
#add_subdirectory(taskflow)
add_subdirectory(tinyexpr)

add_subdirectory(mango)
add_subdirectory(quickhull)

add_subdirectory(erhe)

# Headless ARM soak harness for the intermittent main-loop hang
# (doc/intermittent_main_loop_hang.md). Built on desktop AND Android -- the
# corruption is ARM-only, so the useful runs are on-device via `adb shell`.
add_subdirectory(geogram_soak)

if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
    message(STATUS "Adding subdirectory 'editor'.")
    add_subdirectory(editor)
endif ()

if (NOT ERHE_TARGET_OS_ANDROID)
    add_subdirectory(example)
    add_subdirectory(hello_swap)
    #add_subdirectory(rendering_test)

    if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
        message(STATUS "Adding subdirectory 'hextiles'.")
        add_subdirectory(hextiles)
    endif ()
endif ()
