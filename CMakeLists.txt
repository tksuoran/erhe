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
set_option(ERHE_GRAPHICS_API               "Graphics library to use with erhe. Either vulkan, opengl, metal or none"    "vulkan"   "opengl;vulkan;metal;none")
set_option(ERHE_NAVIGATION_LIBRARY         "Navigation mesh library. Either recastnavigation or none"                   "none"     "recastnavigation;none")
set_option(ERHE_PHYSICS_LIBRARY            "Physics library to use with erhe. Either jolt or none"                      "jolt"     "jolt;none")
set_option(ERHE_PROFILE_LIBRARY            "Profile library to use with erhe. Either nvtx, superluminal, tracy or none" "none"     "nvtx;superluminal;tracy;none")
set_option(ERHE_RAYTRACE_LIBRARY           "Raytrace library to use with erhe. Either embree, bvh, tinybvh or none"     "bvh"      "embree;bvh;tinybvh;none")
set_option(ERHE_SVG_LIBRARY                "SVG loading library. Either plutosvg or none"                               "plutosvg" "plutosvg;none")
set_option(ERHE_TEXT_LAYOUT_LIBRARY        "Text layout library. Either freetype, harfbuzz or none"                     "harfbuzz" "harfbuzz;freetype;none")
set_option(ERHE_WINDOW_LIBRARY             "Window library to use with erhe. Either glfw, sdl or none"                  "sdl"      "sdl;glfw;none")
set_option(ERHE_XR_LIBRARY                 "XR library to use with erhe. Either openxr, or none"                        "none"     "openxr;none")
set_option(ERHE_TERMINAL_LIBRARY           "Terminal use with erhe. Either cpp-terminal, or none"                       "none"     "cpp-terminal;none")
set_option(ERHE_USE_PRECOMPILED_HEADERS    "Use precompiled headers in erhe"                                            "OFF"      "ON;OFF")
set_option(ERHE_USE_ASAN                   "Enable AddressSanitizer"                                                    "OFF"      "ON;OFF")
set_option(ERHE_USE_MIMALLOC               "Enable mimalloc"                                                            "OFF"      "ON;OFF")
set_option(ERHE_USE_FPNG                   "Build the fpng image writer (fast PNG save), independent of window lib"      "ON"       "ON;OFF")
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
        # No Quest-only define here today. ERHE_TARGET_QUEST_STANDALONE
        # used to be set on this branch, but had zero references in the
        # tree. Re-add it (or a successor name) if a Quest-only code
        # path needs it; for the current call sites, gating on ANDROID
        # + __aarch64__ in C/C++ is sufficient.
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
    GIT_TAG           daf9e1925c668a047dc8b50edb7d3591566f5071 # Exact commit for pull_requests (which gets force pushed); includes thread-local ProgressTask fix and the create_sub_elements quadratic growth fix (https://github.com/BrunoLevy/geogram/issues/371)
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

# Geogram's exact geometric predicates REQUIRE that the compiler not fuse their
# arithmetic into FMAs: an FMA changes the rounding of a near-zero orientation
# determinant (and breaks Shewchuk's error-free transformations in the
# multi-precision expansions) -- it "would break exact predicates" in geogram's
# own words. Geogram's Linux platform files set -frounding-math -ffp-contract=off
# for exactly this, but its Android-generic / Darwin platform configs are empty
# and omit it. So on clang (arm64 especially) the default -ffp-contract=on fuses
# the predicates and Geogram's Delaunay locate_inexact() walk loops forever on
# degenerate input (a cone's coplanar base ring) -- the intermittent, ARM-only
# main-loop hang (reproduced headless ~2/10 on arm64, 0/30 on x86/MSVC which does
# not contract by default). Restore geogram's intended flag on every non-MSVC
# compiler. See doc/intermittent_main_loop_hang.md.
if (NOT MSVC)
    foreach (geo_target geogram geogram_third_party geogram_num_3rdparty)
        if (TARGET ${geo_target})
            target_compile_options(${geo_target} PRIVATE -ffp-contract=off)
        endif ()
    endforeach ()
endif ()

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
        # erhe integration branch of glslang (branch `erhe` in tksuoran/glslang =
        # upstream main + downstream fixes), carrying two fixes for upstream bugs in
        # NonSemantic.Shader.DebugInfo emission that spirv-val rejects under
        # VUID-VkShaderModuleCreateInfo-pCode-08737 (strict on Mesa KosmicKrisp /
        # Vulkan-on-Metal; merely tolerated by desktop drivers):
        #   1. Builder::createConstVariable passes the OpConstantComposite id as the
        #      Variable operand of DebugGlobalVariable. Affects every compute shader
        #      (synthesised gl_WorkGroupSize) plus any file-scope const composite
        #      array. See doc/glslang_bug_report_debugglobalvariable.md.
        #   2. makeMemberDebugType emits a DebugTypeMember Column that refers to the
        #      original (pre-preprocessing) source and can exceed the embedded
        #      DebugSource line length (KhronosGroup/glslang #3862, exposed by the
        #      DebugSource line/column check added in SPIRV-Tools #5986 / Vulkan SDK
        #      1.4.350); now emits column 0 like every other DebugInfo instruction.
        # The fork (rather than a CPM PATCHES patch) keeps the build free of any host
        # `patch` invocation - see the "Do not use CPM's PATCHES keyword" note in
        # CLAUDE.md. Move back to KhronosGroup/glslang and bump the tag once upstream
        # ships both fixes.
        GIT_TAG           6ee8c2531efcf16e1531d7a1259dd7100c1db020
        GITHUB_REPOSITORY tksuoran/glslang
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
    OPTIONS
        # The MCP server is a local HTTP/JSON server and uses no content
        # compression. httplib's *_IF_AVAILABLE knobs (default ON) opportunistically
        # enable an optional dependency whenever a matching target already exists in
        # the build tree. cpptrace vendors zstd 1.5.7 (which creates the
        # zstd::libzstd target), so httplib enabled zstd support and propagated
        # CPPHTTPLIB_ZSTD_SUPPORT to the editor target -- but cpptrace's zstd headers
        # are not on the editor's include path on the Android/Quest NDK build, so
        # <zstd.h> was not found. Disable the zstd/brotli auto-enable so httplib's
        # feature set does not depend on incidentally-present vendored targets.
        "HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF"
        "HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF"
)

include(vscode)
vscode_support()

if (${ERHE_WINDOW_LIBRARY} STREQUAL "sdl")
    # SDL is pinned to a commit on the "erhe" branch of the tksuoran/SDL
    # fork. That branch carries erhe's android-project/ changes as commits
    # on top of upstream SDL; erhe's android-project/ directory is a mirror
    # of the branch's android-project/ at exactly this commit, kept in sync
    # by scripts/update_sdl.py (which is also the only supported way to
    # change this pin). To update SDL: rebase the erhe branch in the fork
    # onto the desired upstream ref, push the branch plus a snapshot tag,
    # then run "py -3 scripts/update_sdl.py". See doc/android.md, section
    # "Updating SDL".
    set(ERHE_SDL_GIT_TAG "e3ecf679bf00793c9e0768b816fb86fb6d24a1e1")
    CPMAddPackage(
        NAME              sdl
        GIT_TAG           ${ERHE_SDL_GIT_TAG}
        GIT_SHALLOW       FALSE
        GITHUB_REPOSITORY tksuoran/SDL
        OPTIONS
                    "SDL_STATIC ON"
            "SDL_STATIC_DEFAULT ON"
              "SDL3_MAINPROJECT OFF"
                  "SDL_OPENGLES OFF"
                    "SDL_OPENVR OFF"
                     "SDL_AUDIO OFF"
                    "SDL_RENDER OFF"
    )
    # Drift guard: android-project/ must be the mirror of the SDL commit
    # CPM just fetched; if they disagree the Java shim and the SDL3 C
    # library can diverge, which surfaces as a "SDL C/Java version
    # mismatch" dialog at app launch on Android. SDL_SYNC_COMMIT is
    # written by scripts/update_sdl.py together with ERHE_SDL_GIT_TAG.
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/android-project/SDL_SYNC_COMMIT" ERHE_SDL_SYNC_LINES)
    list(GET ERHE_SDL_SYNC_LINES 0 ERHE_SDL_SYNC_HASH)
    if (NOT ERHE_SDL_SYNC_HASH STREQUAL ERHE_SDL_GIT_TAG)
        message(FATAL_ERROR
            "SDL pin mismatch: ERHE_SDL_GIT_TAG (${ERHE_SDL_GIT_TAG}) does not match "
            "android-project/SDL_SYNC_COMMIT (${ERHE_SDL_SYNC_HASH}). The android-project/ "
            "mirror and the SDL C library would diverge. Do not edit the pin by hand; "
            "run: py -3 scripts/update_sdl.py")
    endif ()
    if (EXISTS "${sdl_SOURCE_DIR}/.git")
        execute_process(
            COMMAND git rev-parse HEAD
            WORKING_DIRECTORY "${sdl_SOURCE_DIR}"
            OUTPUT_VARIABLE ERHE_SDL_CHECKOUT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if (NOT ERHE_SDL_CHECKOUT_HASH STREQUAL ERHE_SDL_SYNC_HASH)
            message(FATAL_ERROR
                "SDL drift: the CPM SDL checkout is at ${ERHE_SDL_CHECKOUT_HASH} but "
                "android-project/ was synced from ${ERHE_SDL_SYNC_HASH} "
                "(android-project/SDL_SYNC_COMMIT). The Java shim and the SDL3 C library "
                "would diverge. Run: py -3 scripts/update_sdl.py")
        endif ()
    endif ()
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

# fpng (https://github.com/richgel999/fpng) is a very fast PNG reader/writer
# used by the erhe::graphics image writer. It is a single source file with no
# external dependencies and is independent of ERHE_WINDOW_LIBRARY, so headless
# builds can save PNGs too. Fetched download-only; fpng.cpp is compiled into
# erhe::graphics directly (see src/erhe/graphics/CMakeLists.txt).
if (ERHE_USE_FPNG)
    CPMAddPackage(
        NAME              fpng
        GIT_TAG           925796543b9d26b8edfcdcecd94c1dac280f29fc
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY richgel999/fpng
        DOWNLOAD_ONLY     YES
    )
    add_definitions(-DERHE_USE_FPNG)
    message(STATUS "Erhe configured to build the fpng image writer (fast PNG save).")
endif ()

if (${ERHE_NAVIGATION_LIBRARY} STREQUAL "recastnavigation")
    CPMAddPackage(
        NAME              recastnavigation
        GIT_TAG           9f4ce64458dfae86e1239c525ddc219c4e9e06f1 # main 2026-06; last release v1.6.0 (2023) predates CMake 3.5+ minimum requirement
        GIT_SHALLOW       TRUE
        GITHUB_REPOSITORY recastnavigation/recastnavigation
        OPTIONS
                "RECASTNAVIGATION_DEMO OFF"
               "RECASTNAVIGATION_TESTS OFF"
            "RECASTNAVIGATION_EXAMPLES OFF"
    )
    message(STATUS "Erhe configured to use recastnavigation for navigation meshes.")
    add_definitions(-DERHE_NAVIGATION_LIBRARY_RECASTNAVIGATION)
else ()
    message(STATUS "Erhe configured to disable navigation meshes.")
    add_definitions(-DERHE_NAVIGATION_LIBRARY_NONE)
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
    GIT_SHALLOW       FALSE
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
        VERSION           0.0.8
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
        # erhe fork carrying (a) KHR_physics_rigid_bodies spec-compliance fixes
        # (mesh-keyed collider geometry, spec inertia key names, exporter JSON
        # fixes), (b) a subset of the glTF 2.1 proposal
        # (KhronosGroup/glTF#2585): unified "files" array, "externalAssets" +
        # Node::externalAssetIndex, minVersion handling, and (c) the
        # KHR_lights_punctual spot-light exporter fix (nested "spot" object;
        # PR'd upstream). Drop the fork when upstream fastgltf ships the
        # current physics spec and glTF 2.1.
        GIT_TAG           af8ff21af5c57d78bb92eccc5b70c6966f06c05f
        #GIT_TAG           v0.9.0
        GITHUB_REPOSITORY tksuoran/fastgltf
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
        # Pinned to a master commit past the v0.13.1 release for the clang-cl
        # (build_ninja_win_clang) build fix. v0.13.1's TracyProfiler.cpp has:
        #   static std::atomic<Thread*> s_sysTraceThread = nullptr;
        # That copy-initialization requires the (deleted) std::atomic copy
        # constructor under clang-cl's pre-C++17 default standard, so the
        # TracyClient target fails to compile (MSVC cl tolerates it, clang-cl
        # does not). Upstream changed it to direct-init "(nullptr)" after the
        # release. Drop this pin and move back to VERSION 0.13.x once a release
        # newer than 0.13.1 ships that change.
        GIT_TAG           4cd6c38932e9ce193ba2ec9775a1374f2e0b3127
        GITHUB_REPOSITORY wolfpld/tracy
        OPTIONS
            # Tracy master defaults TRACY_ENABLE OFF; v0.13.1 defaulted it ON.
            # erhe relied on that default, so enable it explicitly -- otherwise
            # ERHE_PROFILE_LIBRARY=tracy compiles in no profiling and the
            # TracyClient INTERFACE stops defining TRACY_ENABLE for consumers
            # (e.g. editor.cpp's #if TRACY_ENABLE block, which then trips
            # /W4 /WX on an unused variable under MSVC).
            "TRACY_ENABLE ON"
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
        # Jolt's ENABLE_ALL_WARNINGS (default ON) adds /Wall /WX in its native
        # Build/CMakeLists.txt. Under clang-cl, /Wall maps to -Weverything and
        # /WX to -Werror, so warnings its in-source JPH_SUPPRESS_WARNING pragmas
        # do not cover (e.g. -Wpadded on Jolt's own structs) become fatal. cl.exe
        # is unaffected (the pragmas cover its C-numbered warnings) and other
        # compilers want the warnings, so disable Jolt's warnings-as-errors for
        # clang-cl (the MSVC-ABI clang driver) only.
        if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            set(ENABLE_ALL_WARNINGS OFF)
        endif ()
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
    # erhe fork of OpenXR-SDK-Source release-1.1.59 (branch
    # erhe-validation-aliased-extensions) patches the core_validation API
    # layer's codegen so that aliased XrStructureType values (e.g.
    # XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR)
    # accept any one of the alias-introducing extensions, not just the
    # canonical one. Without this, enabling only XR_KHR_vulkan_enable2 and
    # passing XrGraphicsBindingVulkan2KHR triggers a spurious
    # VUID-xrStructureTypeToString-value-parameter that any "warnings are
    # fatal" debug callback escalates to an abort. Not present upstream. The
    # fork (rather than a CPM PATCHES patch) keeps the build free of any host
    # `patch` invocation - see the "Do not use CPM's PATCHES keyword" note in
    # CLAUDE.md. Move back to KhronosGroup/OpenXR-SDK-Source and bump the tag
    # once upstream carries the fix.
    CPMAddPackage(
        NAME              OpenXR-SDK
        VERSION           1.1.59
        GIT_TAG           9e4c7dc2a56e82e2de9a12cb11c13f799783e9f9
        GITHUB_REPOSITORY tksuoran/OpenXR-SDK-Source
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
