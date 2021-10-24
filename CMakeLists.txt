# CMakeLists.txt for erhe

cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

cmake_policy(SET CMP0079 NEW)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

macro(set_option variable_name help_text default_value possible_values)
    set("${variable_name}" "${default_value}" CACHE STRING "${help_text}")
    set_property(CACHE ${variable_name} PROPERTY STRINGS "${possible_values}")
endmacro()

set_option(ERHE_PNG_LIBRARY                "PNG loading library. Either mango or none"                   "mango"    "mango;none")
set_option(ERHE_FONT_RASTERIZATION_LIBRARY "Font rasterization library. Either freetype or none"         "freetype" "freetype;none")
set_option(ERHE_TEXT_LAYOUT_LIBRARY        "Text layout library. Either freetype, harfbuzz or none"      "harfbuzz" "harfbuzz;freetype;none")
set_option(ERHE_WINDOW_LIBRARY             "Window library to use with erhe. Either glfw, mango or none" "glfw"     "glfw;mango;none")
set_option(ERHE_RAYTRACE_LIBRARY           "Raytrace library to use with erhe. Either embree or none"    "embree"   "embree;none")
set_option(ERHE_PHYSICS_LIBRARY            "Physics library to use with erhe. Either bullet or none"     "bullet"   "bullet;none")


# Globally use fastlink
add_link_options($<$<COMPILE_LANGUAGE:CXX>:/DEBUG:FASTLINK>)


include(libraries)


FetchContent_MakeAvailable(nlohmann_json)
FetchContent_MakeAvailable(freetype)
FetchContent_MakeAvailable(harfbuzz)
FetchContent_MakeAvailable(fmt)
#FetchContent_MakeAvailable(spdlog)
FetchContent_MakeAvailable(RectangleBinPack)

# Mango has CMakeLists.txt in build subdirectory
FetchContent_GetProperties(mango)
string(TOLOWER "mango" lc_mango)
if(NOT ${lc_mango}_POPULATED)
    FetchContent_Populate(mango)
    set(MANGO_ALL_ARCHIVE_FORMATS ZIP; RAR; MGX)
    foreach(format ${MANGO_ALL_ARCHIVE_FORMATS})
        option(MANGO_DISABLE_ARCHIVE_${format} "" ON)
    endforeach()

    set(MANGO_ALL_IMAGE_FORMATS ASTC; ATARI; BMP; C64; DDS; GIF; HDR; IFF; JPG; KTX; PCX; PKM; PNM; PVR; SGI; TGA; WEBP; ZPNG)
    foreach(format ${MANGO_ALL_IMAGE_FORMATS})
        option(MANGO_DISABLE_IMAGE_${format} "" ON)
    endforeach()

    add_subdirectory(${${lc_mango}_SOURCE_DIR}/build ${${lc_mango}_BINARY_DIR})
endif()

FetchContent_MakeAvailable(glfw)
FetchContent_MakeAvailable(glm)
FetchContent_MakeAvailable(GSL)
#FetchContent_MakeAvailable(googletest)
FetchContent_MakeAvailable(ImGui)

# TODO Figure out how to disable Tracy profiling properly
#set(TRACY_ENABLE OFF CACHE BOOL "Enable profiling")
#option(TRACY_ENABLE "Enable profiling" OFF)
set(TRACY_ENABLE ON CACHE BOOL "Enable profiling")
option(TRACY_ENABLE "Enable profiling" ON)
FetchContent_MakeAvailable(tracy)

FetchContent_MakeAvailable(cxxopts)


#FetchContent_MakeAvailable(reactphysics3d)
if (${ERHE_PHYSICS_LIBRARY} STREQUAL "bullet")
    FetchContent_MakeAvailable(bullet3)
endif ()
if (${ERHE_RAYTRACE_LIBRARY} STREQUAL "embree")
    FetchContent_MakeAvailable(embree)
endif()

FetchContent_MakeAvailable(OpenXR-SDK)
#FetchContent_MakeAvailable(eigen)
FetchContent_MakeAvailable(avir)
FetchContent_MakeAvailable(lunasvg)

### Require out-of-source builds
file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)
if(EXISTS "${LOC_PATH}")
    message(FATAL_ERROR "You cannot build in a source directory (or any directory with a CMakeLists.txt file). Please make a build subdirectory. Feel free to remove CMakeCache.txt and CMakeFiles.")
endif()

project(erhe VERSION 1.0 LANGUAGES C CXX)

set(ERHE_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/src")

if(CMAKE_CXX_COMPILER_ID MATCHES GNU)
    set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -Wall -Wno-unknown-pragmas -Wno-sign-compare -Woverloaded-virtual -Wwrite-strings -Wno-unused")
    set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g3")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option (FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." TRUE)
if (${FORCE_COLORED_OUTPUT})
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
       add_compile_options (-fdiagnostics-color=always)
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
       add_compile_options (-fcolor-diagnostics)
    endif ()
endif ()

add_compile_options(-DGLM_FORCE_CXX2A=1)
if(("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU") OR ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang"))
    add_compile_options(-Wall)
    add_compile_options(-Wextra)
elseif(DEFINED MSVC)
    include_directories("src/khronos/khronos")
    add_definitions(-DNOMINMAX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)

    # Sadly, CMake does not currently support enabling ASan with MSVC:
    #
    # https://gitlab.kitware.com/cmake/cmake/-/issues/21081
    #
    # This does not work:
    # add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/fsanitize=address>)
endif()

#add_definitions(-DTRACY_ENABLE)

add_definitions(-DFMT_HEADER_ONLY)

add_definitions(-DERHE_DLOAD_ALL_GL_SYMBOLS)

add_definitions(-DGLM_FORCE_DEPTH_ZERO_TO_ONE)

if (${ERHE_PNG_LIBRARY} STREQUAL "mango")
    message(STATUS "Erhe configured to use Mango for PNG loading.")
    add_definitions(-DERHE_PNG_LIBRARY_MANGO)
endif()
if (${ERHE_FONT_RASTERIZATION_LIBRARY} STREQUAL "freetype")
    message(STATUS "Erhe configured to use freetype for font rasterization.")
    add_definitions(-DERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
    ### FetchContent_MakeAvailable(freetype)
endif()
if (${ERHE_TEXT_LAYOUT_LIBRARY} STREQUAL "harfbuzz")
    message(STATUS "Erhe configured to use harfbuzz for text layout.")
    add_definitions(-DERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
    ### FetchContent_MakeAvailable(harfbuzz)
endif()
if (${ERHE_WINDOW_LIBRARY} STREQUAL "glfw")
    message(STATUS "Erhe configured to use glfw for OpenGL Context_window.")
    add_definitions(-DERHE_WINDOW_LIBRARY_GLFW)
endif()
if (${ERHE_WINDOW_LIBRARY} STREQUAL "mango")
    message(STATUS "Erhe configured to use mango for OpenGL Context_window.")
    add_definitions(-DERHE_WINDOW_LIBRARY_MANGO)
endif()
if (${ERHE_RAYTRACE_LIBRARY} STREQUAL "Embree")
    message(STATUS "Erhe configured to use Embree for raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_EMBREE)
endif()
if (${ERHE_RAYTRACE_LIBRARY} STREQUAL "none")
    message(STATUS "Erhe configured to disable raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_NONE)
endif()
if (${ERHE_PHYSICS_LIBRARY} STREQUAL "bullet")
    message(STATUS "Erhe configured to use bullet for physics.")
    add_definitions(-DERHE_PHYSICS_LIBRARY_BULLET)
endif()
if (${ERHE_PHYSICS_LIBRARY} STREQUAL "none")
    message(STATUS "Erhe configured to disable physics.")
    add_definitions(-DERHE_PHYSICS_LIBRARY_NONE)
endif()

# Workaround VS2017
add_definitions(-D_ENABLE_EXTENDED_ALIGNED_STORAGE)

#add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/WX>)

find_package(OpenGL REQUIRED)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/MP>)
endif()

add_subdirectory(src)

if(MSVC)
    set_property(DIRECTORY ${PROJECT_SOURCE_DIR}
                 PROPERTY VS_STARTUP_PROJECT "editor")
endif()


