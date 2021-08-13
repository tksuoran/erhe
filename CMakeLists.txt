# CMakeLists.txt for erhe

cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

cmake_policy(SET CMP0079 NEW)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    #GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
    GIT_TAG        VER-2-10-4
)

FetchContent_Declare(
    harfbuzz
    GIT_REPOSITORY https://github.com/tksuoran/harfbuzz.git
    GIT_TAG        fixes
    #GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    #GIT_TAG        2.7.4
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        7.1.3
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    RectangleBinPack
    GIT_REPOSITORY https://github.com/tksuoran/RectangleBinPack.git
    GIT_TAG        fix-win32
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    ImGui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        master # v1.81
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    avir
    GIT_REPOSITORY https://github.com/avaneev/avir.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    tracy
    #GIT_REPOSITORY https://github.com/tksuoran/tracy.git
    #GIT_TAG        cmake-client
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    reactphysics3d
    GIT_REPOSITORY https://github.com/DanielChappuis/reactphysics3d.git
    GIT_TAG        v0.8.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.8.5
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    bullet3
    #GIT_REPOSITORY https://github.com/bulletphysics/bullet3.git
    GIT_REPOSITORY https://github.com/tksuoran/bullet3.git
    GIT_TAG        cmake-fetchcontent
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.3.9
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    libigl
    GIT_REPOSITORY https://github.com/libigl/libigl.git
    GIT_TAG        v2.3.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    embree
    GIT_REPOSITORY https://github.com/embree/embree.git
    GIT_TAG        v3.13.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)


# Mango project name is about to change.
# Temporarily using my own fork until it comes up with a new name.
FetchContent_Declare(
    mango
    #GIT_REPOSITORY https://github.com/t0rakka/mango.git
    GIT_REPOSITORY https://github.com/tksuoran/mango.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.2
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/tksuoran/glm-1.git
    GIT_TAG        fix-cmake
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    GSL
    GIT_REPOSITORY https://github.com/microsoft/GSL.git
    GIT_TAG        main # v3.1.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)


FetchContent_Declare(
    OpenXR-SDK
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)


#FetchContent_Declare(
#    concurrentqueue
#    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
#    GIT_TAG        master
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)


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
    add_subdirectory(${${lc_mango}_SOURCE_DIR}/build ${${lc_mango}_BINARY_DIR})
endif()

FetchContent_MakeAvailable(glfw)
FetchContent_MakeAvailable(glm)
FetchContent_MakeAvailable(GSL)
#FetchContent_MakeAvailable(googletest)
FetchContent_MakeAvailable(ImGui)
FetchContent_MakeAvailable(tracy)
#FetchContent_MakeAvailable(reactphysics3d)
FetchContent_MakeAvailable(bullet3)
FetchContent_MakeAvailable(embree)

FetchContent_MakeAvailable(OpenXR-SDK)
#FetchContent_MakeAvailable(eigen)
#FetchContent_MakeAvailable(libigl)
#FetchContent_MakeAvailable(concurrentqueue)

### Require out-of-source builds
file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)
if(EXISTS "${LOC_PATH}")
    message(FATAL_ERROR "You cannot build in a source directory (or any directory with a CMakeLists.txt file). Please make a build subdirectory. Feel free to remove CMakeCache.txt and CMakeFiles.")
endif()

project(erhe VERSION 1.0 LANGUAGES C CXX)

set(ERHE_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/src")

# set(CMAKE_CXX_CPPCHECK "C:/Program\ Files/Cppcheck/cppcheck.exe")
#set(CMAKE_CXX_CPPCHECK cppcheck)
#list(APPEND CMAKE_CXX_CPPCHECK
#    "-j 4"
#    "--quiet"
#    "--enable=information"
#    "--enable=portability"
#    "--enable=performance"
#    "--enable=style"
#    "--enable=warning"
#    #"--enable=error"
#    "--std=c++20"
#    "--platform=unix64"
#    # "--clang=clang-12"
#)

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

set(ERHE_PNG_LIBRARY "Mango" CACHE STRING "PNG loading library. Either Mango or none")
set_property(CACHE ERHE_PNG_LIBRARY PROPERTY STRINGS none Mango)

set(ERHE_FONT_RASTERIZATION_LIBRARY "freetype" CACHE STRING "Font rasterization library. Either freetype or none")
set_property(CACHE ERHE_FONT_RASTERIZATION_LIBRARY PROPERTY STRINGS none freetype)

set(ERHE_FONT_LAYOUT_LIBRARY "harfbuzz" CACHE STRING "Font layout library. Either freetype, harfbuzz or none")
set_property(CACHE ERHE_FONT_LAYOUT_LIBRARY PROPERTY STRINGS none freetype harfbuzz)

set(ERHE_WINDOW_TOOLKIT "GLFW" CACHE STRING "Window toolkit to use with erhe. Either GLFW or Mango")
set_property(CACHE ERHE_WINDOW_TOOLKIT PROPERTY STRINGS GLFW Mango)

if (${ERHE_PNG_LIBRARY} STREQUAL "Mango")
    message(STATUS "Erhe configured to use Mango for PNG loading.")
    add_definitions(-DERHE_PNG_LIBRARY_MANGO)
endif()
if (${ERHE_FONT_RASTERIZATION_LIBRARY} STREQUAL "freetype")
    message(STATUS "Erhe configured to use freetype for font rasterization.")
    add_definitions(-DERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
    ### FetchContent_MakeAvailable(freetype)
endif()
if (${ERHE_FONT_LAYOUT_LIBRARY} STREQUAL "harfbuzz")
    message(STATUS "Erhe configured to use harfbuzz for font layout.")
    add_definitions(-DERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
    ### FetchContent_MakeAvailable(harfbuzz)
endif()
if (${ERHE_WINDOW_TOOLKIT} STREQUAL "GLFW")
    message(STATUS "Erhe configured to use GLFW for OpenGL Context_window.")
    add_definitions(-DERHE_WINDOW_TOOLKIT_GLFW)
endif()
if (${ERHE_WINDOW_TOOLKIT} STREQUAL "Mango")
    message(STATUS "Erhe configured to use Mango for OpenGL Context_window.")
    add_definitions(-DERHE_WINDOW_TOOLKIT_MANGO)
endif()

### if (${ERHE_PNG_LIBRARY} STREQUAL "Mango" OR ${ERHE_WINDOW_TOOLKIT} STREQUAL "Mango")
###     FetchContent_GetProperties(mango)
###     string(TOLOWER "mango" lc_mango)
###     if(NOT ${lc_mango}_POPULATED)
###         FetchContent_Populate(mango)
###         add_subdirectory(${${lc_mango}_SOURCE_DIR}/build ${${lc_mango}_BINARY_DIR})
###     endif()
### endif()

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

# Add Analyze with CppCheck target if CppCheck is installed
# if(WIN32)
#   # Find CppCheck executable
#   find_program(CMAKE_CXX_CPPCHECK cppcheck NAMES cppcheck HINTS $ENV{PROGRAMFILES}/cppcheck)
# 
#   # If CppCheck executable found
#   if(CMAKE_CXX_CPPCHECK)
#     # Check CppCheck version
#     set(CPP_CHECK_CMD ${CMAKE_CXX_CPPCHECK} --version)
#     execute_process(COMMAND ${CPP_CHECK_CMD}
#       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
#       RESULT_VARIABLE CPP_CHECK_RESULT
#       OUTPUT_VARIABLE CPP_CHECK_VERSION
#       ERROR_VARIABLE CPP_CHECK_ERROR)
# 
#     # Check if version could be extracted
#     if(CPP_CHECK_RESULT EQUAL 0)
#       # Get number of CPU cores
#       include(ProcessorCount)
#       ProcessorCount(CPU_CORES)
# 
#       # Append desired arguments to CppCheck
#       list(
#         APPEND CMAKE_CXX_CPPCHECK
# 
#           # Using the below template will allow jumping to any found error from inside Visual Studio output window by double click
#           "--template \"${CMAKE_SOURCE_DIR}/{file}({line}): {severity} ({id}): {message}\""
# 
#           # Use all the available CPU cores
#           "-j ${CPU_CORES}"
# 
#           # Only show found errors
#           "--quiet"
# 
#           # Desired warning level in CppCheck
#           "--enable=style"
# 
#           # Optional: Specified C++ version
#           "--std=c++17"
# 
#           # Optional: Specified platform
#           "--platform=win64"
# 
#           # Optional: suppression file stored in same directory as the top level CMake script
#           "--suppressions-list=${CMAKE_SOURCE_DIR}/cppcheck_suppressions.txt"
# 
#           # Optional: Use inline suppressions
#           "--inline-suppr"
# 
#           # Run CppCheck from the working directory, as specified in the add_custom_target command below
#           "."
#         )
# 
#       add_custom_target(ANALYZE_CPPCHECK DEPENDS erhe
#         COMMAND ${CMAKE_CXX_CPPCHECK}
#         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#         COMMENT "Static code analysis using ${CPP_CHECK_VERSION}"
#       )
#     endif()
#   endif()
# endif()
