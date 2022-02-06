# CMakeLists.txt for erhe

# Require out-of-source builds
file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)
if (EXISTS "${LOC_PATH}")
    message(FATAL_ERROR "You cannot build in a source directory (or any directory with a CMakeLists.txt file). Please make a build subdirectory. Feel free to remove CMakeCache.txt and CMakeFiles.")
endif ()

cmake_policy(SET CMP0048 NEW) # The project() command manages VERSION variables.
cmake_policy(SET CMP0079 NEW) # target_link_libraries() allows use with targets in other directories.
cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)

project(
    erhe
    VERSION      1.0
    HOMEPAGE_URL "https://github.com/tksuoran/erhe"
    LANGUAGES    C CXX
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(FETCHCONTENT_BASE_DIR "${PROJECT_SOURCE_DIR}/../cache")
set(ERHE_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/src")

# Split parts to cmake/
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Helper function to declare cache variables to control erhe options
macro (set_option variable_name help_text default_value possible_values)
    set("${variable_name}" "${default_value}" CACHE STRING "${help_text}")
    set_property(CACHE ${variable_name} PROPERTY STRINGS "${possible_values}")
endmacro ()

# Options to control which third party libraries are used with erhe
set_option(ERHE_PNG_LIBRARY                "PNG loading library. Either fpng, mango or none"                      "none"     "fpng;mango;none")
set_option(ERHE_FONT_RASTERIZATION_LIBRARY "Font rasterization library. Either freetype or none"                  "freetype" "freetype;none")
set_option(ERHE_TEXT_LAYOUT_LIBRARY        "Text layout library. Either freetype, harfbuzz or none"               "harfbuzz" "harfbuzz;freetype;none")
set_option(ERHE_WINDOW_LIBRARY             "Window library to use with erhe. Either glfw or none"                 "glfw"     "glfw;none")
set_option(ERHE_RAYTRACE_LIBRARY           "Raytrace library to use with erhe. Either embree or none"             "embree"   "embree;none")
set_option(ERHE_PHYSICS_LIBRARY            "Physics library to use with erhe. Either bullet or none"              "bullet"   "bullet;none")
set_option(ERHE_PROFILE_LIBRARY            "Profile library to use with erhe. Either superluminal, tracy or none" "none"     "superluminal;tracy;none")
set_option(ERHE_GLTF_LIBRARY               "GLTF library to use with erhe. Either glTF-SDK, or none"              "none"     "glTF-SDK;none")
set_option(ERHE_XR_LIBRARY                 "XR library to use with erhe. Either OpenXR, or none"                  "none"     "OpenXR;none")

# These are in cmake/ directory
message("Compiler = ${CMAKE_CXX_COMPILER_ID}")
include(${CMAKE_CXX_COMPILER_ID})
include(common_options)
include(functions)
include(glfw)
include(libraries)
include(mango)
include(tracy)
include(vscode)

# Dependencies
message("Fetching dependencies")
message("Fetching concurrentqueue")
FetchContent_MakeAvailable(concurrentqueue)
message("Fetching cxxopts")
FetchContent_MakeAvailable(cxxopts)
message("Fetching fmt")
FetchContent_MakeAvailable(fmt)
message("Fetching glfw")
FetchContent_MakeAvailable_glfw()
message("Fetching glm")
FetchContent_MakeAvailable(glm)
message("Fetching GSL")
FetchContent_MakeAvailable(GSL)
message("Fetching lunasvg")
FetchContent_MakeAvailable(lunasvg)
message("Fetching nlohmann_json")
FetchContent_MakeAvailable(nlohmann_json)

if (${ERHE_XR_LIBRARY} STREQUAL "OpenXR")
    message("Fetching OpenXR-SDK")
    FetchContent_MakeAvailable(OpenXR-SDK)
    message(STATUS "Erhe configured to use OpenXR for XR.")
    add_definitions(-DERHE_XR_LIBRARY_OPENXR)
else ()
    message(STATUS "Erhe configured to disable XR.")
    add_definitions(-DERHE_XR_LIBRARY_NONE)
endif ()

if (${ERHE_GLTF_LIBRARY} STREQUAL "glTF-SDK")
    message("Fetching glTF-SDK")
    FetchContent_MakeAvailable(glTF-SDK)
    message(STATUS "Erhe configured to use glTF-SDK for glTF.")
    add_definitions(-DERHE_GLTF_LIBRARY_GLTF_SDK)
else () #elseif (${ERHE_GLTF_LIBRARY} STREQUAL "glTF-SDK")
    message(STATUS "Erhe configured to disable glTF.")
    add_definitions(-DERHE_GLTF_LIBRARY_NONE)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    message("Fetching tracy")
    FetchContent_MakeAvailable_tracy()
    message(STATUS "Erhe configured to use Tracy for profiling.")
    add_definitions(-DERHE_PROFILE_LIBRARY_TRACY)
elseif (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    message(STATUS "Erhe configured to use Superluminal for profiling.")
    add_definitions(-DERHE_PROFILE_LIBRARY_SUPERLUMINAL)
else ()
    message(STATUS "Erhe configured to use disable for instrumented profiling.")
    add_definitions(-DERHE_PROFILE_LIBRARY_NONE)
endif ()


vscode_support()

if (${ERHE_PNG_LIBRARY} STREQUAL "mango")
    message("Fetching mango")
    FetchContent_MakeAvailable_mango()
    message(STATUS "Erhe configured to use Mango for PNG loading.")
    add_definitions(-DERHE_PNG_LIBRARY_MANGO)
else ()
    message(STATUS "Erhe configured to disable PNG loading.")
    add_definitions(-DERHE_PNG_LIBRARY_NONE)
endif ()

if (${ERHE_FONT_RASTERIZATION_LIBRARY} STREQUAL "freetype")
    message("Fetching freetype")
    FetchContent_MakeAvailable(freetype)
    message(STATUS "Erhe configured to use freetype for font rasterization.")
    add_definitions(-DERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
else ()
    message(STATUS "Erhe configured to disable font rasterization.")
    add_definitions(-DERHE_FONT_RASTERIZATION_LIBRARY_NONE)
endif ()

if (${ERHE_TEXT_LAYOUT_LIBRARY} STREQUAL "harfbuzz")
    message("Fetching harfbuzz")
    FetchContent_MakeAvailable(harfbuzz)
    message(STATUS "Erhe configured to use harfbuzz for text layout.")
    add_definitions(-DERHE_TEXT_LAYOUT_LIBRARY_HARFBUZZ)
else ()
    message(STATUS "Erhe configured to disable text layout.")
    add_definitions(-DERHE_TEXT_LAYOUT_LIBRARY_NONE)
endif ()

if (${ERHE_WINDOW_LIBRARY} STREQUAL "glfw")
    message(STATUS "Erhe configured to use glfw for OpenGL context window.")
    add_definitions(-DERHE_WINDOW_LIBRARY_GLFW)
else ()
    message(STATUS "Erhe configured to disable OpenGL Context window.")
    add_definitions(-DERHE_WINDOW_LIBRARY_NONE)
endif ()

if (${ERHE_RAYTRACE_LIBRARY} STREQUAL "embree")
    message("Fetching Embree")
    FetchContent_MakeAvailable(embree)
    message(STATUS "Erhe configured to use Embree for raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_EMBREE)
else ()
    message(STATUS "Erhe configured to disable raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_NONE)
endif ()

if (${ERHE_PHYSICS_LIBRARY} STREQUAL "bullet")
    message("Fetching bullet3")
    FetchContent_MakeAvailable(bullet3)
    message(STATUS "Erhe configured to use bullet for physics.")
    add_definitions(-DERHE_PHYSICS_LIBRARY_BULLET)
else ()
    message(STATUS "Erhe configured to disable physics.")
    add_definitions(-DERHE_PHYSICS_LIBRARY_NONE)
endif ()

message("Finding OpenGL")
find_package(OpenGL REQUIRED)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

message("src")
add_subdirectory(src)

if (MSVC)
    set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "editor")
endif ()
