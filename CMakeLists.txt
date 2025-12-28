# CMakeLists.txt for erhe

# Require out-of-source builds
file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)
if (EXISTS "${LOC_PATH}")
    message(FATAL_ERROR "You cannot build in a source directory (or any directory with a CMakeLists.txt file). Please make a build subdirectory. Feel free to remove CMakeCache.txt and CMakeFiles.")
endif ()

cmake_policy(SET CMP0048 NEW) # The `project()` command manages `VERSION `variables.
cmake_policy(SET CMP0072 NEW) # `FindOpenGL` prefers GLVND by default when available.
cmake_policy(SET CMP0075 NEW) # Include file check macros honor `CMAKE_REQUIRED_LIBRARIES`.
cmake_policy(SET CMP0076 NEW) # The `target_sources()` command converts relative paths to absolute.
cmake_policy(SET CMP0079 NEW) # `target_link_libraries()` allows use with targets in other directories.
cmake_policy(SET CMP0091 NEW) # MSVC runtime library flags are selected by an abstraction.
cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)

project(
    erhe
    VERSION      1.0
    HOMEPAGE_URL "https://github.com/tksuoran/erhe"
    LANGUAGES    C CXX
)

include(cmake/CPM.cmake)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(ERHE_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/src")

# Split parts to cmake/
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Helper function to declare cache variables to control erhe options
macro (set_option variable_name help_text default_value possible_values)
    set("${variable_name}" "${default_value}" CACHE STRING "${help_text}")
    set_property(CACHE ${variable_name} PROPERTY STRINGS "${possible_values}")
endmacro ()

# Options to control which third party libraries are used with erhe
set_option(ERHE_GRAPHICS_LIBRARY           "Graphics library to use with erhe. Either opengl or vulkan"                 "opengl"   "opengl;vulkan")
set_option(ERHE_WINDOW_LIBRARY             "Window library to use with erhe. Either glfw, sdl or none"                  "sdl"      "sdl;glfw;none")
set_option(ERHE_USE_PRECOMPILED_HEADERS    "Use precompiled headers in erhe"                                            "OFF"      "ON;OFF")

# These are in cmake/ directory
message("Compiler = ${CMAKE_CXX_COMPILER_ID}")
include(${CMAKE_CXX_COMPILER_ID})
include(common_options)
include(functions)

if (WIN32)
    message("Detected Windows platform")
    set(ERHE_TARGET_OS_WINDOWS TRUE)
    add_definitions(-DERHE_OS_WINDOWS)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
else ()
    message(FATAL_ERROR "OS not supported ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif ()

# Dependencies
message("Fetching dependencies")

CPMAddPackage(
    NAME              fmt
    GIT_TAG           12.0.0
    VERSION           12.0.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY fmtlib/fmt
)

CPMAddPackage(
    NAME              spdlog
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY gabime/spdlog
    OPTIONS "SPDLOG_FMT_EXTERNAL ON"
)

CPMAddPackage(
    NAME              glm
    VERSION           1.0.2
    GIT_TAG           1.0.2
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY g-truc/glm
)
# Enable GLM_GTX_component_wise
add_definitions(-DGLM_ENABLE_EXPERIMENTAL)

if (${ERHE_GRAPHICS_LIBRARY} STREQUAL "vulkan")
    message(STATUS "Erhe configured to use vulkan as graphics API.")
    set(ERHE_GRAPHICS_API_VULKAN TRUE)
    add_definitions(-DERHE_GRAPHICS_LIBRARY_VULKAN)
    CPMAddPackage(
        NAME              Vulkan-Headers
        GIT_TAG           vulkan-sdk-1.4.328.1
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY KhronosGroup/Vulkan-Headers
    )
    CPMAddPackage(
        NAME              VulkanMemoryAllocator
        VERSION           3.3.0
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    )
    CPMAddPackage(
        NAME              volk
        #VERSION           vulkan-sdk-1.4.328.1
        #GIT_TAG           vulkan-sdk-1.4.328.1
        GIT_TAG           4f3bcee79618a9abe79f4c717c50379197c77512
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY zeux/volk
    )
endif ()

include(vscode)
vscode_support()

if (${ERHE_WINDOW_LIBRARY} STREQUAL "sdl")
    CPMAddPackage(
        NAME              sdl
        VERSION           release-3.2.24
        GIT_TAG           release-3.2.24
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY libsdl-org/sdl
        OPTIONS
                    "SDL_STATIC ON"
            "SDL_STATIC_DEFAULT ON"
              "SDL3_MAINPROJECT OFF"
                  "SDL_OPENGLES OFF"
                    "SDL_OPENVR OFF"
                     "SDL_AUDIO OFF"
                    "SDL_RENDER OFF"
    )
    message(STATUS "Erhe configured to use SDL for window system integration.")
    add_definitions(-DERHE_WINDOW_LIBRARY_SDL)
else ()
    message(STATUS "Erhe configured to disable window system integration.")
    add_definitions(-DERHE_WINDOW_LIBRARY_NONE)
endif ()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(src)

if (MSVC)
    set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "hello_swap")
endif ()
