# CMakeLists.txt for erhe/src

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS        NO)

#add_subdirectory(hidapi)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    add_subdirectory(superluminal)
endif ()

if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
    add_subdirectory(imgui)
endif ()

if (${ERHE_AUDIO_LIBRARY} STREQUAL "miniaudio")
    add_subdirectory(miniaudio)
endif ()

add_subdirectory(mINI)
add_subdirectory(rapidjson)
add_subdirectory(RectangleBinPack)
#add_subdirectory(taskflow)
add_subdirectory(tinyexpr)

add_subdirectory(mango)

add_subdirectory(erhe)

if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
    add_subdirectory(editor)
endif ()    

add_subdirectory(example)

if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
    add_subdirectory(hextiles)
endif ()
