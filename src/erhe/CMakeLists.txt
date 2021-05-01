# CMakeLists.txt for src/erhe

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS        NO)

add_subdirectory(components)
add_subdirectory(geometry)
add_subdirectory(gl)
add_subdirectory(graphics)
add_subdirectory(graphics_experimental)
add_subdirectory(imgui)
add_subdirectory(log)
add_subdirectory(primitive)
add_subdirectory(scene)
add_subdirectory(toolkit)
add_subdirectory(ui)
