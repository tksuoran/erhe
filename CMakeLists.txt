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
set(CMAKE_WARN_DEPRECATED FALSE)

project(
    erhe
    VERSION      1.0
    HOMEPAGE_URL "https://github.com/tksuoran/erhe"
    LANGUAGES    C CXX
)

# Use a persistent source cache so CPM downloads survive --fresh rebuilds.
# Override with -DCPM_SOURCE_CACHE=<path> or the CPM_SOURCE_CACHE env var.
# Must be set before including CPM.cmake, which reads it at include time.
if (NOT CPM_SOURCE_CACHE AND NOT DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_SOURCE_CACHE "${PROJECT_SOURCE_DIR}/.cpm_cache" CACHE PATH "CPM source cache directory")
endif()

include(cmake/CPM.cmake)

# Pass -DFETCHCONTENT_FULLY_DISCONNECTED=ON to skip all network access
# when sources are already cached (fastest possible reconfigure).

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
set_option(ERHE_AUDIO_LIBRARY              "Audio library. Either miniaudio or none"                                    "none"     "miniaudio;none")
set_option(ERHE_FONT_RASTERIZATION_LIBRARY "Font rasterization library. Either freetype or none"                        "freetype" "freetype;none")
set_option(ERHE_GLTF_LIBRARY               "GLTF library. Either fastgltf or none"                                      "fastgltf" "fastgltf;none")
set_option(ERHE_GUI_LIBRARY                "GUI library. Either imgui or none"                                          "imgui"    "imgui;none")
set_option(ERHE_GRAPHICS_API           "Graphics library to use with erhe. Either opengl, vulkan, metal or none"    "opengl"   "opengl;vulkan;metal;none")
set_option(ERHE_PHYSICS_LIBRARY            "Physics library to use with erhe. Either jolt or none"                      "jolt"     "jolt;none")
set_option(ERHE_PROFILE_LIBRARY            "Profile library to use with erhe. Either nvtx, superluminal, tracy or none" "none"     "nvtx;superluminal;tracy;none")
set_option(ERHE_RAYTRACE_LIBRARY           "Raytrace library to use with erhe. Either embree, bvh, tinybvh or none"    "bvh"      "embree;bvh;tinybvh;none")
set_option(ERHE_SVG_LIBRARY                "SVG loading library. Either plutosvg or none"                               "plutosvg" "plutosvg;none")
set_option(ERHE_TEXT_LAYOUT_LIBRARY        "Text layout library. Either freetype, harfbuzz or none"                     "harfbuzz" "harfbuzz;freetype;none")
set_option(ERHE_WINDOW_LIBRARY             "Window library to use with erhe. Either glfw, sdl or none"                  "sdl"      "sdl;glfw;none")
set_option(ERHE_XR_LIBRARY                 "XR library to use with erhe. Either openxr, or none"                        "none"     "openxr;none")
set_option(ERHE_TERMINAL_LIBRARY           "Terminal use with erhe. Either cpp-terminal, or none"                       "none"     "cpp-terminal;none")
set_option(ERHE_USE_PRECOMPILED_HEADERS    "Use precompiled headers in erhe"                                            "OFF"      "ON;OFF")
set_option(ERHE_USE_ASAN                   "Enable AddressSanitizer"                                                    "OFF"      "ON;OFF")
set_option(ERHE_USE_MIMALLOC               "Enable mimalloc"                                                            "OFF"      "ON;OFF")
set_option(ERHE_SPIRV                      "Enable SPIR-V"                                                              "OFF"      "ON;OFF")
set_option(ERHE_BUILD_TESTS                "Build erhe unit tests"                                                      "OFF"      "ON;OFF")

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    enable_testing()
endif ()

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
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
elseif (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    message("Detected Linux platform")
    set(ERHE_TARGET_OS_LINUX TRUE)
    add_definitions(-DERHE_OS_LINUX)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WAYLAND_KHR)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    message("Detected Apple OSX")
    set(ERHE_TARGET_OS_OSX TRUE)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_MACOS_MVK)
    add_definitions(-DERHE_OS_MACOS)
    # Ad-hoc code signing so that all executables can run without manual codesign
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    message("Detected Android platform")
    set(ERHE_TARGET_OS_ANDROID TRUE)
    add_definitions(-DERHE_OS_ANDROID)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_ANDROID_KHR)
else ()
    message(FATAL_ERROR "OS not supported ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif ()

# On Android, force a known-good subset of options. OpenGL is desktop-only,
# Embree is x86-only, and mimalloc / Tracy are not yet ported.
#
# OpenXR is enabled only for the `quest` Gradle product flavor (Meta Quest 3),
# selected via -DERHE_ANDROID_FLAVOR=quest forwarded by Gradle. The Khronos
# OpenXR-SDK loader (already CPM-fetched for desktop) builds cleanly on the
# NDK toolchain and produces a libopenxr_loader.so packaged into the APK.
if (ERHE_TARGET_OS_ANDROID)
    set(ERHE_GRAPHICS_API "vulkan"  CACHE STRING "" FORCE)
    set(ERHE_WINDOW_LIBRARY   "sdl"     CACHE STRING "" FORCE)
    if (ERHE_ANDROID_FLAVOR STREQUAL "quest")
        set(ERHE_XR_LIBRARY "openxr" CACHE STRING "" FORCE)
        # Surfaces an ERHE_TARGET_QUEST_STANDALONE compile definition for
        # code paths that need to work around behaviours specific to the
        # Quest's on-device OpenXR runtime (Meta's libvrapiimpl.so). The
        # define MUST NOT cover Quest builds reached via Meta Quest Link
        # / Air Link or any desktop OpenXR runtime; those run as desktop
        # builds where this CMake branch does not apply, so no extra
        # guard is needed.
        add_compile_definitions(ERHE_TARGET_QUEST_STANDALONE=1)
    else ()
        set(ERHE_XR_LIBRARY "none"   CACHE STRING "" FORCE)
    endif ()
    set(ERHE_RAYTRACE_LIBRARY "bvh"     CACHE STRING "" FORCE)
    set(ERHE_PROFILE_LIBRARY  "none"    CACHE STRING "" FORCE)
    set(ERHE_USE_MIMALLOC     OFF       CACHE BOOL   "" FORCE)
    set(ERHE_USE_ASAN         OFF       CACHE BOOL   "" FORCE)
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
    # The geogram fork ships an "Android-generic" platform definition. The
    # arm64/gcc/dynamic variants are not present; use the generic profile.
    set(VORPALINE_PLATFORM "Android-generic")
endif()

CPMAddPackage(
    NAME              geogram
    GIT_TAG           acb0c9da # Exact commit for pull_requests (which gets force pushed)
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY tksuoran/geogram
    # Currently erhe still requires some pull-requests and we must use fork above instead
    #VERSION            1.9.8
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
if (geogram_SOURCE_DIR AND EXISTS "${geogram_SOURCE_DIR}/.git")
    execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY "${geogram_SOURCE_DIR}"
        OUTPUT_VARIABLE ERHE_GEOGRAM_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(ERHE_GEOGRAM_GIT_COMMIT "unknown")
endif()
add_definitions(-DERHE_GEOGRAM_GIT_COMMIT="${ERHE_GEOGRAM_GIT_COMMIT}")

CPMAddPackage(
    NAME              cpptrace
    GIT_TAG           v1.0.4
    VERSION           1.0.4
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY jeremy-rifkin/cpptrace
)

CPMAddPackage(
    NAME              fmt
    GIT_TAG           12.1.0
    VERSION           12.1.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY fmtlib/fmt
)

CPMAddPackage(
    NAME              spdlog
    VERSION           1.17.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY gabime/spdlog
    OPTIONS "SPDLOG_FMT_EXTERNAL ON"
)

CPMAddPackage(
    NAME              concurrentqueue 
    GIT_TAG           master
    #GIT_TAG           00dd7ba09164fcd25b590818747e3d9a32dddc9c
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
    VERSION           20.47.1
    GIT_TAG           20.47.1
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY ETLCPP/etl
)

CPMAddPackage(
    NAME              glm
    VERSION           1.0.3
    GIT_TAG           1.0.3
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY g-truc/glm
)
# Enable GLM_GTX_component_wise
add_definitions(-DGLM_ENABLE_EXPERIMENTAL)

# Force ERHE_SPIRV on for Metal and Vulkan before the glslang fetch
if (${ERHE_GRAPHICS_API} STREQUAL "metal" OR ${ERHE_GRAPHICS_API} STREQUAL "vulkan")
    set(ERHE_SPIRV "ON" CACHE STRING "Enable SPIR-V" FORCE)
endif ()

if (ERHE_SPIRV)
    CPMAddPackage(
        NAME              glslang
        VERSION           16.3.0
        GIT_TAG           16.3.0
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY KhronosGroup/glslang
        # Local patch for an upstream bug in glslang 16.3.0 (still
        # present): Builder::createConstVariable passes the
        # OpConstantComposite id through as the Variable operand of
        # DebugGlobalVariable, which spirv-val 1.4.328+ rejects under
        # VUID-VkShaderModuleCreateInfo-pCode-08737. Affects every
        # compute shader (synthesised gl_WorkGroupSize) plus any
        # file-scope const composite array. See
        # doc/glslang_bug_report_debugglobalvariable.md and the
        # patch file's header for the full write-up. Drop this once
        # KhronosGroup/glslang ships a release containing the fix.
        PATCHES
            "${CMAKE_CURRENT_LIST_DIR}/cmake/patches/glslang-debugglobalvariable-noop-for-composites.patch"
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

if (${ERHE_GRAPHICS_API} STREQUAL "opengl")
    message(STATUS "Erhe configured to use OpenGL as graphics API.")
    set(ERHE_GRAPHICS_API_OPENGL TRUE)
    add_definitions(-DERHE_GRAPHICS_API_OPENGL)
    find_package(OpenGL REQUIRED)
elseif (${ERHE_GRAPHICS_API} STREQUAL "vulkan")
    message(STATUS "Erhe configured to use vulkan as graphics API.")
    set(ERHE_GRAPHICS_API_VULKAN TRUE)
    add_definitions(-DERHE_GRAPHICS_API_VULKAN)
    add_definitions(-DVK_NO_PROTOTYPES)
    CPMAddPackage(
        NAME              Vulkan-Headers
        GIT_TAG           vulkan-sdk-1.4.341.0
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY KhronosGroup/Vulkan-Headers
    )
    CPMAddPackage(
        NAME              VulkanMemoryAllocator
        VERSION           3.3.0
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    )
    set(VOLK_STATIC_DEFINES VK_NO_PROTOTYPES CACHE STRING "" FORCE)
    # Force volk to use the CPM-fetched Vulkan-Headers instead of an installed
    # Vulkan SDK ($ENV{VULKAN_SDK}) or whatever find_package(Vulkan) locates.
    # This is volk's highest-priority lookup (see VOLK_PULL_IN_VULKAN logic).
    set(VULKAN_HEADERS_INSTALL_DIR "${Vulkan-Headers_SOURCE_DIR}" CACHE PATH "Use CPM Vulkan-Headers" FORCE)
    CPMAddPackage(
        NAME              volk
        GIT_TAG           vulkan-sdk-1.4.341.0
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY zeux/volk
    )
elseif (${ERHE_GRAPHICS_API} STREQUAL "metal")
    if (NOT APPLE)
        message(FATAL_ERROR "Metal backend is only available on Apple platforms (macOS, iOS)")
    endif()
    message(STATUS "Erhe configured to use Metal as graphics API.")
    set(ERHE_GRAPHICS_API_METAL TRUE)
    add_definitions(-DERHE_GRAPHICS_API_METAL)
    CPMAddPackage(
        NAME              metal-cpp
        GITHUB_REPOSITORY bkaradzic/metal-cpp
        GIT_TAG           metal-cpp_macOS15.2_iOS18.2
        GIT_SHALLOW       TRUE
    )
    CPMAddPackage(
        NAME              SPIRV-Cross
        GITHUB_REPOSITORY KhronosGroup/SPIRV-Cross
        GIT_TAG           vulkan-sdk-1.4.341.0
        GIT_SHALLOW       TRUE
    )
elseif (${ERHE_GRAPHICS_API} STREQUAL "none")
    message(STATUS "Erhe configured to use null graphics backend (headless).")
    set(ERHE_GRAPHICS_API_NONE TRUE)
    add_definitions(-DERHE_GRAPHICS_API_NONE)
endif ()

CPMAddPackage(
    NAME              nlohmann_json
    VERSION           3.12.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY nlohmann/json
)

CPMAddPackage(
    NAME              httplib
    VERSION           0.43.1
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY yhirose/cpp-httplib
)

include(vscode)
vscode_support()

if (${ERHE_WINDOW_LIBRARY} STREQUAL "sdl")
    CPMAddPackage(
        NAME              sdl
        # Pinned to the commit android-project/ was copied from. The Java
        # shim (SDLActivity.java etc.) under android-project/app/src/main/
        # java/org/libsdl/app/ must match the SDL3 C library version - any
        # drift triggers a "SDL version mismatch" dialog at app launch.
        GIT_TAG           03ceaca19
        GIT_SHALLOW       FALSE
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
    message(STATUS "Erhe configured to use glfw for window system integration.")
    add_definitions(-DERHE_WINDOW_LIBRARY_GLFW)
else ()
    message(STATUS "Erhe configured to disable window system integration.")
    add_definitions(-DERHE_WINDOW_LIBRARY_NONE)
endif ()

CPMAddPackage(
    NAME              simdjson
    VERSION           4.6.3
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY simdjson/simdjson
)

# CPMAddPackage(
#     NAME              reflect-cpp
#     GIT_SHALLOW       TRUE
#     GIT_TAG           main
#     GITHUB_REPOSITORY getml/reflect-cpp
# )

CPMAddPackage(
    NAME              taskflow
    VERSION           4.0.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY taskflow/taskflow
    OPTIONS
           "TF_BUILD_TESTS OFF"
        "TF_BUILD_EXAMPLES OFF"
)

CPMAddPackage(
    NAME              wuffs
    GIT_TAG           126a80a8f1dd62c40ca6391dc37094538dca795a
    #VERSION           0.4.0-alpha.9
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
        GIT_TAG           cc385fd83ebf890507c8cae37e3fe96cf166fed1
        #VERSION           0.0.7
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
        GIT_TAG           a31be255ff041fa1d6697e5040fef3c9b6405795
        #GIT_TAG           v0.9.0
        GITHUB_REPOSITORY spnda/fastgltf
        OPTIONS
                  "FASTGLTF_ENABLE_DEPRECATED_EXT ON"
                       "FASTGLTF_COMPILE_AS_CPP20 ON"
        "FASTGLTF_ENABLE_KHR_PHYSICS_RIGID_BODIES ON"
                         "FASTGLTF_ENABLE_INSTALL OFF"
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
        VERSION           3.5.0
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY NVIDIA/NVTX
    )
    message(STATUS "Erhe configured to use nvtx for profiling.")
    add_definitions(-DERHE_PROFILE_LIBRARY_NVTX)
    set(ERHE_PROFILE_TARGET nvtx3-cpp) # CACHE STRING "" FORCE)
elseif (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    CPMAddPackage(
        NAME              tracy
        VERSION           0.13.1
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY wolfpld/tracy
        OPTIONS
            "TRACY_ON_DEMAND ON"
    )
    message(STATUS "Erhe configured to use Tracy for profiling.")
    add_definitions(-DERHE_PROFILE_LIBRARY_TRACY)
    set(ERHE_PROFILE_TARGET Tracy::TracyClient) # CACHE STRING "" FORCE)
elseif (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    message(STATUS "Erhe configured to use Superluminal for profiling.")
    add_definitions(-DERHE_PROFILE_LIBRARY_SUPERLUMINAL)
    set(ERHE_PROFILE_TARGET superluminal)
else ()
    message(STATUS "Erhe configured to use disable for instrumented profiling.")
    add_definitions(-DERHE_PROFILE_LIBRARY_NONE)
endif ()

if (ERHE_USE_MIMALLOC)
    if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
        CPMAddPackage(
            NAME              mimalloc
            GIT_TAG           tracy
            GIT_SHALLOW       TRUE
            GITHUB_REPOSITORY tksuoran/mimalloc
            OPTIONS
                    "MI_OVERRIDE OFF"
                "MI_WIN_REDIRECT OFF"
                "MI_BUILD_SHARED OFF"
                "MI_BUILD_STATIC ON"
                "MI_BUILD_OBJECT OFF"
                "MI_BUILD_TESTS OFF"
                "MI_TRACK_TRACY ON"
        )
        message(STATUS "Erhe configured to use mimalloc with Tracy tracking.")
    else ()
        CPMAddPackage(
            NAME              mimalloc
            GIT_TAG           tracy
            GIT_SHALLOW       TRUE
            GITHUB_REPOSITORY tksuoran/mimalloc
            OPTIONS
                "MI_OVERRIDE ON"
        )
        message(STATUS "Erhe configured to use mimalloc without tracking.")
    endif ()
    add_definitions(-DERHE_USE_MIMALLOC)
else ()
    message(STATUS "Erhe configured to NOT use mimalloc.")
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
        VERSION           2.14.3
        GIT_TAG           VER-2-14-3
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
        GIT_TAG           14.2.0
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
    CPMAddPackage(
        NAME              embree
        VERSION           4.4.1
        GITHUB_REPOSITORY RenderKit/embree
        OPTIONS
            "EMBREE_ISPC_SUPPORT OFF"
            "EMBREE_TUTORIALS OFF"
            "EMBREE_TESTING_INTENSITY 0"
            "EMBREE_TASKING_SYSTEM INTERNAL"
            "EMBREE_STATIC_LIB ON"
    )
    message(STATUS "Erhe configured to use Embree for raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_EMBREE)
    set(ERHE_RAYTRACE_TARGET embree)
elseif (${ERHE_RAYTRACE_LIBRARY} STREQUAL "bvh")
    CPMAddPackage(
        NAME              bvh
        GIT_TAG           ac41ab88a32d0c247009d8ed456fd2795c1ee023
        #GIT_TAG           master
        GITHUB_REPOSITORY madmann91/bvh
    )
    message(STATUS "Erhe configured to use bvh for raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_BVH)
    set(ERHE_RAYTRACE_TARGET bvh)
elseif (${ERHE_RAYTRACE_LIBRARY} STREQUAL "tinybvh")
    CPMAddPackage(
        NAME              tinybvh
        GIT_TAG           4735685f64d8b2bb0ad2c6c4643f291bf58774ad
        GITHUB_REPOSITORY jbikker/tinybvh
        DOWNLOAD_ONLY     YES
    )
    if (tinybvh_ADDED)
        add_library(tinybvh INTERFACE)
        target_include_directories(tinybvh INTERFACE "${tinybvh_SOURCE_DIR}")
    endif()
    message(STATUS "Erhe configured to use tinybvh for raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_TINYBVH)
    set(ERHE_RAYTRACE_TARGET tinybvh)
else ()
    message(STATUS "Erhe configured to disable raytracing.")
    add_definitions(-DERHE_RAYTRACE_LIBRARY_NONE)
endif ()
if (bvh_SOURCE_DIR AND EXISTS "${bvh_SOURCE_DIR}/.git")
    execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY "${bvh_SOURCE_DIR}"
        OUTPUT_VARIABLE ERHE_BVH_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(ERHE_BVH_GIT_COMMIT "unknown")
endif()
add_definitions(-DERHE_BVH_GIT_COMMIT="${ERHE_BVH_GIT_COMMIT}")
if (tinybvh_SOURCE_DIR AND EXISTS "${tinybvh_SOURCE_DIR}/.git")
    execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY "${tinybvh_SOURCE_DIR}"
        OUTPUT_VARIABLE ERHE_TINYBVH_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(ERHE_TINYBVH_GIT_COMMIT "unknown")
endif()
add_definitions(-DERHE_TINYBVH_GIT_COMMIT="${ERHE_TINYBVH_GIT_COMMIT}")

if (${ERHE_PHYSICS_LIBRARY} STREQUAL "jolt")
    CPMAddPackage(
        NAME              JoltPhysics
        GIT_TAG           v5.5.0
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
    # Patch the core_validation API layer's codegen so that aliased
    # XrStructureType values (e.g. XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR
    # = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR) accept any one of the
    # alias-introducing extensions, not just the canonical one. Without
    # this, enabling only XR_KHR_vulkan_enable2 and passing
    # XrGraphicsBindingVulkan2KHR triggers a spurious
    # VUID-xrStructureTypeToString-value-parameter that any "warnings
    # are fatal" debug callback escalates to an abort. See the patch
    # file's header for the full write-up. Drop this once the upstream
    # fix lands in OpenXR-SDK-Source.
    CPMAddPackage(
        NAME              OpenXR-SDK
        VERSION           1.1.59
        GIT_TAG           release-1.1.59
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY KhronosGroup/OpenXR-SDK-Source
        PATCHES
            "${CMAKE_CURRENT_LIST_DIR}/cmake/patches/openxr-sdk-validation-accept-aliased-extensions.patch"
        OPTIONS
            "BUILD_API_LAYERS ON"
                 "BUILD_TESTS OFF"

    )
    message(STATUS "Erhe configured to use OpenXR for XR.")
    add_definitions(-DERHE_XR_LIBRARY_OPENXR)
else ()
    message(STATUS "Erhe configured to disable XR.")
    add_definitions(-DERHE_XR_LIBRARY_NONE)
endif ()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(src)

if (MSVC)
    set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "editor")
endif ()
