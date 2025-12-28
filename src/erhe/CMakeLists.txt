# CMakeLists.txt for src/erhe

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS        NO)

if (${ERHE_USE_PRECOMPILED_HEADERS})
    add_subdirectory(pch)
endif()

add_subdirectory(log)
add_subdirectory(utility)
add_subdirectory(dataformat)
add_subdirectory(defer)
add_subdirectory(graphics)
add_subdirectory(hash)
add_subdirectory(verify)
add_subdirectory(window)
