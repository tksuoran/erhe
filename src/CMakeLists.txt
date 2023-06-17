# CMakeLists.txt for erhe/src

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS        NO)

add_subdirectory(hidapi)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    add_subdirectory(superluminal)
endif ()

if (${ERHE_GLTF_LIBRARY} STREQUAL "cgltf")
    add_subdirectory(cgltf)
endif ()

if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
    add_subdirectory(imgui)
endif ()

add_subdirectory(mikktspace)

if (${ERHE_AUDIO_LIBRARY} STREQUAL "miniaudio")
    add_subdirectory(miniaudio)
endif ()

add_subdirectory(mINI)
add_subdirectory(rapidjson)
add_subdirectory(RectangleBinPack)

if (${ERHE_PNG_LIBRARY} STREQUAL "mango")
    add_subdirectory(spng)
    add_subdirectory(mango)
endif ()

add_subdirectory(erhe)
add_subdirectory(editor)
add_subdirectory(example)

if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
    add_subdirectory(hextiles)
endif ()
