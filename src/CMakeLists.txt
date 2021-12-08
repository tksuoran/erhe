# CMakeLists.txt for erhe/src

if (WIN32)
    add_subdirectory(hidapi)
endif ()
if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    add_subdirectory(superluminal)
endif ()
add_subdirectory(imgui)
add_subdirectory(mikktspace)
add_subdirectory(miniaudio)
add_subdirectory(erhe)
add_subdirectory(editor)
