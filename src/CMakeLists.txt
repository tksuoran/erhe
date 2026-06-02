# CMakeLists.txt for erhe/src

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS        NO)

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
