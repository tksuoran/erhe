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
add_subdirectory(imgui)
add_subdirectory(mikktspace)
add_subdirectory(miniaudio)
add_subdirectory(RectangleBinPack)
add_subdirectory(erhe)
add_subdirectory(editor)
