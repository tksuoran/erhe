# CMakeLists.txt for erhe/src

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS        NO)

if (WIN32)
    add_subdirectory(hidapi)
endif ()
if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    add_subdirectory(superluminal)
endif ()
if (${ERHE_GLTF_LIBRARY} STREQUAL "cgltf")
    add_subdirectory(cgltf)
endif ()
add_subdirectory(imgui)
add_subdirectory(ghc_filesystem)
add_subdirectory(mikktspace)

if (${ERHE_AUDIO_LIBRARY} STREQUAL "miniaudio")
    add_subdirectory(miniaudio)
endif ()

add_subdirectory(optional_lite)
add_subdirectory(RectangleBinPack)
add_subdirectory(erhe)
add_subdirectory(editor)
add_subdirectory(hextiles)
#add_subdirectory(PowerBattle)
