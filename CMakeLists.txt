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

#set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Do not update everytime I run cmake, please")

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
set_option(ERHE_GLTF_LIBRARY               "GLTF library. Either fastgltf or none"                                      "fastgltf" "fastgltf;none")
set_option(ERHE_GUI_LIBRARY                "GUI library. Either imgui or none"                                          "imgui"    "imgui;none")
set_option(ERHE_GRAPHICS_LIBRARY           "Graphics library to use with erhe. Either opengl or vulkan"                 "opengl"   "opengl;vulkan")
set_option(ERHE_PHYSICS_LIBRARY            "Physics library to use with erhe. Either jolt or none"                      "jolt"     "jolt;none")
set_option(ERHE_PROFILE_LIBRARY            "Profile library to use with erhe. Either nvtx, superluminal, tracy or none" "none"     "nvtx;superluminal;tracy;none")
set_option(ERHE_RAYTRACE_LIBRARY           "Raytrace library to use with erhe. Either embree, bvh or none"              "bvh"      "embree;bvh;none")
set_option(ERHE_SVG_LIBRARY                "SVG loading library. Either plutosvg or none"                               "plutosvg" "plutosvg;none")
set_option(ERHE_TEXT_LAYOUT_LIBRARY        "Text layout library. Either freetype, harfbuzz or none"                     "harfbuzz" "harfbuzz;freetype;none")
set_option(ERHE_WINDOW_LIBRARY             "Window library to use with erhe. Either glfw, sdl or none"                  "sdl"      "sdl;glfw;none")
set_option(ERHE_XR_LIBRARY                 "XR library to use with erhe. Either openxr, or none"                        "none"     "openxr;none")
set_option(ERHE_TERMINAL_LIBRARY           "Terminal use with erhe. Either cpp-terminal, or none"                       "none"     "cpp-terminal;none")
set_option(ERHE_USE_PRECOMPILED_HEADERS    "Use precompiled headers in erhe"                                            "OFF"      "ON;OFF")
set_option(ERHE_USE_ASAN                   "Enable AddressSanitizer"                                                    "OFF"      "ON;OFF")
set_option(ERHE_SPIRV                      "Enable SPIR-V"                                                              "OFF"      "ON;OFF")

# TODO fix ERHE_USE_PRECOMPILED_HEADERS

# These are in cmake/ directory
message("Compiler = ${CMAKE_CXX_COMPILER_ID}")
include(${CMAKE_CXX_COMPILER_ID})
include(common_options)
include(functions)

if (WIN32)
    message("Detected Windows platform")
    set(ERHE_TARGET_OS_WINDOWS TRUE)
    add_definitions(-DERHE_OS_WINDOWS)
elseif (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    message("Detected Linux platform")
    set(ERHE_TARGET_OS_LINUX TRUE)
    add_definitions(-DERHE_OS_LINUX)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    message("Detected Apple OSX")
    set(ERHE_TARGET_OS_OSX TRUE)
    add_definitions(-DERHE_OS_OSX)
else ()
    message(FATAL_ERROR "OS not supported ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif ()

if (${ERHE_PHYSICS_LIBRARY} STREQUAL "jolt")
    include(JoltPhysicsCompatibility)
endif ()

# Dependencies
message("Fetching dependencies")

if(CMAKE_SYSTEM_NAME MATCHES Linux)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES x86_64)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            set(VORPALINE_PLATFORM "Linux64-gcc-dynamic")
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(VORPALINE_PLATFORM "Linux64-clang-dynamic")
        endif()
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            set(VORPALINE_PLATFORM "Linux64-nonx86-gcc-dynamic")
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(VORPALINE_PLATFORM "Linux64-nonx86-clang-dynamic")
        endif()
    endif()
elseif(CMAKE_SYSTEM_NAME MATCHES Darwin)
    set(VORPALINE_PLATFORM "Darwin-clang-dynamic")
elseif(CMAKE_SYSTEM_NAME MATCHES Windows)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        set(VORPALINE_PLATFORM "Win64-gcc" CACHE STRING "")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        set(VORPALINE_PLATFORM "Win-vs-generic" CACHE STRING "")
    endif()
elseif(CMAKE_SYSTEM_NAME MATCHES Android)
    set(VORPALINE_PLATFORM "Android-aarch64-gcc-dynamic")
endif()

CPMAddPackage(
    NAME              geogram
    #GIT_TAG           clion
    GIT_TAG           pull_requests
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY tksuoran/geogram
    #VERSION            1.9.5
    #GITHUB_REPOSITORY  BrunoLevy/geogram
    OPTIONS
                  "VORPALINE_PLATFORM ${VORPALINE_PLATFORM}"
                   "GEOGRAM_SUB_BUILD ON"
               "GEOGRAM_WITH_GRAPHICS OFF"
        "GEOGRAM_WITH_LEGACY_NUMERICS OFF"
                 "GEOGRAM_WITH_HLBFGS ON"
                 "GEOGRAM_WITH_TETGEN ON"
               "GEOGRAM_WITH_TRIANGLE ON"
                    "GEOGRAM_WITH_LUA OFF"
                    "GEOGRAM_LIB_ONLY ON"
                    "GEOGRAM_WITH_FPG ON"
            "GEOGRAM_USE_SYSTEM_GLFW3 OFF"
              "GEOGRAM_WITH_GARGANTUA OFF"
                    "GEOGRAM_WITH_TBB OFF"
)

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
    NAME              concurrentqueue 
    GIT_TAG           master
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY cameron314/concurrentqueue
)

CPMAddPackage(
    NAME              cxxopts 
    VERSION           3.3.1
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY jarro2783/cxxopts
)

CPMAddPackage(
    NAME              etl
    VERSION           20.44.1
    GIT_TAG           20.44.1
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY ETLCPP/etl
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

if (ERHE_SPIRV)
    CPMAddPackage(
        NAME              glslang
        VERSION           16.0.0
        GIT_TAG           16.0.0
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY KhronosGroup/glslang
        OPTIONS
                      "GLSLANG_TESTS OFF"
             "GLSLANG_ENABLE_INSTALL OFF"
                       "ENABLE_SPIRV ON"
                 "ENABLE_SPVREMAPPER OFF"
            "ENABLE_GLSLANG_BINARIES OFF"
                  "ENABLE_GLSLANG_JS OFF"
                        "ENABLE_HLSL OFF"
                        "ENABLE_RTTI ON"
                  "ENABLE_EXCEPTIONS ON"
                         "ENABLE_OPT OFF"
                         "ENABLE_PCH ${ERHE_USE_PRECOMPILED_HEADERS}"
    )
    add_definitions(-DERHE_SPIRV)
endif ()

if (${ERHE_GRAPHICS_LIBRARY} STREQUAL "opengl")
    message(STATUS "Erhe configured to use OpenGL as graphics API.")
    add_definitions(-DERHE_GRAPHICS_LIBRARY_OPENGL)
elseif (${ERHE_GRAPHICS_LIBRARY} STREQUAL "vulkan")
    message(STATUS "Erhe configured to use vulkan as graphics API.")
    add_definitions(-DERHE_GRAPHICS_LIBRARY_VULKAN)
endif ()

CPMAddPackage(
    NAME              nlohmann_json
    VERSION           3.12.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY nlohmann/json
)

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
    message(STATUS "Erhe configured to use SDL for OpenGL context window.")
    add_definitions(-DERHE_WINDOW_LIBRARY_SDL)
elseif (${ERHE_WINDOW_LIBRARY} STREQUAL "glfw")
    CPMAddPackage(
        NAME              glfw
        VERSION           3.4
        GIT_TAG           3.4
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY glfw/glfw
        OPTIONS
              "BUILD_SHARED_LIBS OFF"
            "GLFW_BUILD_EXAMPLES OFF"
               "GLFW_BUILD_TESTS OFF"
                "GLFW_BUILD_DOCS OFF"
                   "GLFW_INSTALL OFF"
             "GLFW_VULKAN_STATIC OFF"
    )
    message(STATUS "Erhe configured to use glfw for OpenGL context window.")
    add_definitions(-DERHE_WINDOW_LIBRARY_GLFW)
else ()
    message(STATUS "Erhe configured to disable OpenGL Context window.")
    add_definitions(-DERHE_WINDOW_LIBRARY_NONE)
endif ()

CPMAddPackage(
    NAME              simdjson
    VERSION           4.0.7
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY simdjson/simdjson
)

CPMAddPackage(
    NAME              tomlplusplus
    GIT_SHALLOW       TRUE
    VERSION           3.4.0
    GITHUB_REPOSITORY marzer/tomlplusplus
)

# CPMAddPackage(
#     NAME              reflect-cpp
#     GIT_SHALLOW       TRUE
#     GIT_TAG           main
#     GITHUB_REPOSITORY getml/reflect-cpp
# )

CPMAddPackage(
    NAME              taskflow
    VERSION           3.10.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY taskflow/taskflow
    OPTIONS
           "TF_BUILD_TESTS OFF"
        "TF_BUILD_EXAMPLES OFF"
)

CPMAddPackage(
    NAME              wuffs
    VERSION           0.4.0-alpha.9
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/wuffs
)

if (${ERHE_TERMINAL_LIBRARY} STREQUAL "cpp-terminal")
    CPMAddPackage(NAME cpp-terminal GITHUB_REPOSITORY jupyter-xeus/cpp-terminal)
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

if (${ERHE_SVG_LIBRARY} STREQUAL "plutosvg")
    CPMAddPackage(
        NAME              plutosvg
        VERSION           0.0.7
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY sammycage/plutosvg
    )
    message(STATUS "Erhe configured to use plutosvg for SVG.")
    add_definitions(-DERHE_SVG_LIBRARY_PLUTOSVG)
else ()
    message(STATUS "Erhe configured to disable SVG.")
    add_definitions(-DERHE_SVG_LIBRARY_NONE)
endif ()

if (${ERHE_GLTF_LIBRARY} STREQUAL "fastgltf")
    CPMAddPackage(
        NAME              fastgltf
        GIT_TAG           v0.9.0
        GITHUB_REPOSITORY spnda/fastgltf
        OPTIONS
                  "FASTGLTF_ENABLE_DEPRECATED_EXT ON"
                       "FASTGLTF_COMPILE_AS_CPP20 ON"
        "FASTGLTF_ENABLE_KHR_PHYSICS_RIGID_BODIES ON"
    )
    message(STATUS "Erhe configured to use fastgltf for glTF.")
    add_definitions(-DERHE_GLTF_LIBRARY_FASTGLTF)
else ()
    message(STATUS "Erhe configured to disable glTF.")
    add_definitions(-DERHE_GLTF_LIBRARY_NONE)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "nvtx")
    CPMAddPackage(
        NAME              nvtx
        VERSION           3.3.0
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY NVIDIA/NVTX
    )
    message(STATUS "Erhe configured to use nvtx for profiling.")
    add_definitions(-DERHE_PROFILE_LIBRARY_NVTX)
    set(ERHE_PROFILE_TARGET nvtx3-cpp) # CACHE STRING "" FORCE)
elseif (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    CPMAddPackage(
        NAME              tracy
        VERSION           0.12.2
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY wolfpld/tracy
        OPTIONS 
    )
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

if (${ERHE_FONT_RASTERIZATION_LIBRARY} STREQUAL "freetype")
    CPMAddPackage(
        NAME              freetype
        VERSION           2.14.1
        GIT_TAG           VER-2-14-1
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY freetype/freetype
    )
    message(STATUS "Erhe configured to use freetype for font rasterization.")
    add_definitions(-DERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
else ()
    message(STATUS "Erhe configured to disable font rasterization.")
    add_definitions(-DERHE_FONT_RASTERIZATION_LIBRARY_NONE)
endif ()

if (${ERHE_TEXT_LAYOUT_LIBRARY} STREQUAL "harfbuzz")
    CPMAddPackage(
        NAME              harfbuzz
        GIT_TAG           12.1.0
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY harfbuzz/harfbuzz
    )
    message(STATUS "Erhe configured to use harfbuzz for text layout.")
    add_definitions(-DERHE_TEXT_LAYOUT_LIBRARY_HARFBUZZ)
else ()
    message(STATUS "Erhe configured to disable text layout.")
    add_definitions(-DERHE_TEXT_LAYOUT_LIBRARY_NONE)
endif ()

if (${ERHE_RAYTRACE_LIBRARY} STREQUAL "embree")
    CPMAddPackage(NAME embree VERSION 3.13.3 GITHUB_REPOSITORY embree/embree)
    message(STATUS "Erhe configured to use Embree for raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_EMBREE)
    set(ERHE_RAYTRACE_TARGET embree)
elseif (${ERHE_RAYTRACE_LIBRARY} STREQUAL "bvh")
    CPMAddPackage(
        NAME              bvh
        GIT_TAG           fbdc59615112d471409515cd2ab826fcc3100fd7
        #GIT_TAG           master
        GITHUB_REPOSITORY madmann91/bvh
    )
    message(STATUS "Erhe configured to use bvh for raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_BVH)
    set(ERHE_RAYTRACE_TARGET bvh)
else ()
    message(STATUS "Erhe configured to disable raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_NONE)
endif ()

if (${ERHE_PHYSICS_LIBRARY} STREQUAL "jolt")
    CPMAddPackage(
        NAME              JoltPhysics
        GIT_TAG           v5.4.0
        GITHUB_REPOSITORY jrouwe/JoltPhysics
        DOWNLOAD_ONLY     YES
    )
    if (JoltPhysics_ADDED)
        set(OVERRIDE_CXX_FLAGS OFF)
        set(CROSS_PLATFORM_DETERMINISTIC ON)
        set(INTERPROCEDURAL_OPTIMIZATION OFF)
        set(FLOATING_POINT_EXCEPTIONS_ENABLED ON)
        set(CPP_EXCEPTIONS_ENABLED ON)
        set(CPP_RTTI_ENABLED ON)
        set(ENABLE_INSTALL OFF)
        set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF)
        add_subdirectory(${JoltPhysics_SOURCE_DIR}/Build ${JoltPhysics_BINARY_DIR})
    endif ()
    message(STATUS "Erhe configured to use jolt for physics.")
    add_definitions(-DERHE_PHYSICS_LIBRARY_JOLT)
else ()
    message(STATUS "Erhe configured to disable physics.")
    add_definitions(-DERHE_PHYSICS_LIBRARY_NONE)
endif ()

if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    CPMAddPackage(
        NAME              OpenXR-SDK
        VERSION           1.1.53
        GIT_TAG           release-1.1.53
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY KhronosGroup/OpenXR-SDK
    )
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
