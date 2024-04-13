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

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
#set(FETCHCONTENT_BASE_DIR "${PROJECT_SOURCE_DIR}/../cache")
set(ERHE_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/src")

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Do not update everytime I run cmake, please")

# Split parts to cmake/
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Helper function to declare cache variables to control erhe options
macro (set_option variable_name help_text default_value possible_values)
    set("${variable_name}" "${default_value}" CACHE STRING "${help_text}")
    set_property(CACHE ${variable_name} PROPERTY STRINGS "${possible_values}")
endmacro ()

# Options to control which third party libraries are used with erhe
set_option(ERHE_AUDIO_LIBRARY              "Audio library. Either miniaudio or none"                                    "none"     "miniaudio;none")
set_option(ERHE_FONT_RASTERIZATION_LIBRARY "Font rasterization library. Either freetype or none"                        "freetype" "freetype;none")
set_option(ERHE_GLTF_LIBRARY               "GLTF library. Either cgltf or none"                                         "cgltf"    "cgltf;none")
set_option(ERHE_GUI_LIBRARY                "GUI library. Either imgui or none"                                          "imgui"    "imgui;none")
set_option(ERHE_PHYSICS_LIBRARY            "Physics library to use with erhe. Either bullet, jolt or none"              "jolt"     "bullet;jolt;none")
set_option(ERHE_PNG_LIBRARY                "PNG loading library. Either mango or none"                                  "mango"    "mango;none")
set_option(ERHE_PROFILE_LIBRARY            "Profile library to use with erhe. Either nvtx, superluminal, tracy or none" "none"     "nvtx;superluminal;tracy;none")
set_option(ERHE_RAYTRACE_LIBRARY           "Raytrace library to use with erhe. Either embree, bvh or none"              "bvh"      "embree;bvh;none")
set_option(ERHE_SVG_LIBRARY                "SVG loading library. Either lunasvg or none"                                "lunasvg"  "lunasvg;none")
set_option(ERHE_TEXT_LAYOUT_LIBRARY        "Text layout library. Either freetype, harfbuzz or none"                     "harfbuzz" "harfbuzz;freetype;none")
set_option(ERHE_WINDOW_LIBRARY             "Window library to use with erhe. Either glfw or none"                       "glfw"     "glfw;none")
set_option(ERHE_XR_LIBRARY                 "XR library to use with erhe. Either openxr, or none"                        "none"     "openxr;none")
set_option(ERHE_TERMINAL_LIBRARY           "Terminal use with erhe. Either cpp-terminal, or none"                       "none"     "cpp-terminal;none")
set_option(ERHE_USE_PRECOMPILED_HEADERS    "Use precompiled headers in erhe"                                            "ON"       "ON;OFF")

# These are in cmake/ directory
message("Compiler = ${CMAKE_CXX_COMPILER_ID}")
include(${CMAKE_CXX_COMPILER_ID})
include(common_options)
include(functions)
include(libraries)

if (WIN32)
    set(ERHE_TARGET_OS_WINDOWS TRUE)
    add_definitions(-DERHE_OS_WINDOWS)
elseif (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(ERHE_TARGET_OS_LINUX TRUE)
    add_definitions(-DERHE_OS_LINUX)
else ()
    message(FATAL_ERROR "OS not supported ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif ()

if (${ERHE_PHYSICS_LIBRARY} STREQUAL "jolt")
    include(JoltPhysicsCompatibility)
endif ()

# Dependencies
message("Fetching dependencies")

message("Fetching fmt")
FetchContent_MakeAvailable(fmt)

message("Fetching spdlog")
include(spdlog)
FetchContent_MakeAvailable_spdlog()

message("Fetching igl")
include(igl)
FetchContent_MakeAvailable_igl()

message("Fetching concurrentqueue")
FetchContent_MakeAvailable(concurrentqueue)

message("Fetching cxxopts")
FetchContent_MakeAvailable(cxxopts)

message("Fetching etl")
FetchContent_MakeAvailable(etl)

message("Fetching glm")
FetchContent_MakeAvailable(glm)
# Enable GLM_GTX_component_wise
add_definitions(-DGLM_ENABLE_EXPERIMENTAL)

message("Fetching glslang")
include(glslang)
FetchContent_MakeAvailable_glslang()

message("Fetching GSL")
FetchContent_MakeAvailable(GSL)

#message("Fetching MathGeoLib")
#FetchContent_MakeAvailable(MathGeoLib)

#message("Fetching meshoptimizer")
#FetchContent_MakeAvailable(meshoptimizer)

message("Fetching nlohmann_json")
FetchContent_MakeAvailable(nlohmann_json)

include(vscode)
vscode_support()

include(taskflow)
FetchContent_MakeAvailable_taskflow()

if (${ERHE_TERMINAL_LIBRARY} STREQUAL "cpp-terminal")
    message("Fetching cpp-terminal")
    FetchContent_MakeAvailable(cpp-terminal)
endif ()

if (${ERHE_TERMINAL_LIBRARY} STREQUAL "cpp-terminal")
    message("Fetching cpp-terminal")
    FetchContent_MakeAvailable(cpp-terminal)
    message(STATUS "Erhe configured to use cpp-terminal.")
    add_definitions(-DERHE_TERMINAL_LIBRARY_CPP_TERMINAL)
else ()
    message(STATUS "Erhe configured not use terminal library / to use standard IO as terminal.")
    add_definitions(-DERHE_TERMINAL_LIBRARY_NONE)
endif ()

if (${ERHE_AUDIO_LIBRARY} STREQUAL "miniaudio")
    message(STATUS "Erhe configured to use miniaudio for audio.")
    add_definitions(-DERHE_AUDIO_LIBRARY_MINIAUDIO)
else ()
    message(STATUS "Erhe configured to disable audio.")
    add_definitions(-DERHE_AUDIO_LIBRARY_NONE)
endif ()

if (${ERHE_SVG_LIBRARY} STREQUAL "lunasvg")
    message("Fetching lunasvg")
    FetchContent_MakeAvailable(lunasvg)
    message(STATUS "Erhe configured to use lunasvg for SVG.")
    add_definitions(-DERHE_SVG_LIBRARY_LUNASVG)
else ()
    message(STATUS "Erhe configured to disable SVG.")
    add_definitions(-DERHE_SVG_LIBRARY_NONE)
endif ()

if (${ERHE_GLTF_LIBRARY} STREQUAL "cgltf")
    message("Fetching cgltf")
    FetchContent_MakeAvailable(cgltf)
    message(STATUS "Erhe configured to use cgltf for glTF.")
    add_definitions(-DERHE_GLTF_LIBRARY_CGLTF)
else ()
    message(STATUS "Erhe configured to disable glTF.")
    add_definitions(-DERHE_GLTF_LIBRARY_NONE)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "nvtx")
    message("Fetching nvtx")
    include(nvtx)
    FetchContent_MakeAvailable_nvtx()
    message(STATUS "Erhe configured to use nvtx for profiling.")
    add_definitions(-DERHE_PROFILE_LIBRARY_NVTX)
    set(ERHE_PROFILE_TARGET nvtx3-cpp) # CACHE STRING "" FORCE)
elseif (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    message("Fetching tracy")
    include(tracy)
    FetchContent_MakeAvailable_tracy()
    message(STATUS "Erhe configured to use Tracy for profiling.")
    add_definitions(-DERHE_PROFILE_LIBRARY_TRACY)
    set(ERHE_PROFILE_TARGET TracyClient) # CACHE STRING "" FORCE)
elseif (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    message(STATUS "Erhe configured to use Superluminal for profiling.")
    add_definitions(-DERHE_PROFILE_LIBRARY_SUPERLUMINAL)
    set(ERHE_PROFILE_TARGET superluminal)
else ()
    message(STATUS "Erhe configured to use disable for instrumented profiling.")
    add_definitions(-DERHE_PROFILE_LIBRARY_NONE)
endif ()

if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
    message(STATUS "Erhe configured to use ImGui for GUI.")
    add_definitions(-DERHE_GUI_LIBRARY_IMGUI)
else ()
    message(STATUS "Erhe configured to disable GUI.")
    add_definitions(-DERHE_GUI_LIBRARY_NONE)
endif ()

if (${ERHE_PNG_LIBRARY} STREQUAL "mango")
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
    message("Fetching glfw")
    include(glfw)
    FetchContent_MakeAvailable_glfw()
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
    set(ERHE_RAYTRACE_TARGET embree)
elseif (${ERHE_RAYTRACE_LIBRARY} STREQUAL "bvh")
    message("Fetching bvh")
    FetchContent_MakeAvailable(bvh)
    message(STATUS "Erhe configured to use bvh for raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_BVH)
    set(ERHE_RAYTRACE_TARGET bvh)
else ()
    message(STATUS "Erhe configured to disable raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_NONE)
endif ()

if (${ERHE_PHYSICS_LIBRARY} STREQUAL "jolt")
    message("Fetching JoltPhysics")
    include(JoltPhysics)
    FetchContent_MakeAvailable_JoltPhysics()
    message(STATUS "Erhe configured to use jolt for physics.")
    add_definitions(-DERHE_PHYSICS_LIBRARY_JOLT)
elseif (${ERHE_PHYSICS_LIBRARY} STREQUAL "bullet")
    message("Fetching bullet3")
    FetchContent_MakeAvailable(bullet3)
    message(STATUS "Erhe configured to use bullet for physics.")
    add_definitions(-DERHE_PHYSICS_LIBRARY_BULLET)
else ()
    message(STATUS "Erhe configured to disable physics.")
    add_definitions(-DERHE_PHYSICS_LIBRARY_NONE)
endif ()

if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    message("Fetching OpenXR-SDK")
    FetchContent_MakeAvailable(OpenXR-SDK)
    message(STATUS "Erhe configured to use OpenXR for XR.")
    add_definitions(-DERHE_XR_LIBRARY_OPENXR)
else ()
    message(STATUS "Erhe configured to disable XR.")
    add_definitions(-DERHE_XR_LIBRARY_NONE)
endif ()

find_package(OpenGL REQUIRED)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(src)

if (MSVC)
    set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "editor")
endif ()
