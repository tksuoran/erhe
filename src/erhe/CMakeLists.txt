# CMakeLists.txt for src/erhe

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS        NO)

add_subdirectory(application)
add_subdirectory(components)
add_subdirectory(concurrency)
add_subdirectory(geometry)
add_subdirectory(gl)
add_subdirectory(graphics)
add_subdirectory(log)
add_subdirectory(message_bus)
add_subdirectory(physics)
add_subdirectory(primitive)
add_subdirectory(raytrace)
add_subdirectory(scene)
add_subdirectory(toolkit)
add_subdirectory(ui)

if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    add_subdirectory(xr)
endif ()
