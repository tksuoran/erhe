# CMakeLists.txt for erhe/src

if (WIN32)
    add_subdirectory(hidapi)
endif ()
add_subdirectory(imgui)
add_subdirectory(mikktspace)
add_subdirectory(erhe)
add_subdirectory(editor)
