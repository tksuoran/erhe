include(FetchContent)

#CPMAddPackage(
#    NAME bvh
#    #VERSION 1.2.1
#    GITHUB_REPOSITORY madmann91/bvh
#    #OPTIONS
#    #"BUILD_SHARED_LIBS OFF" "ENABLE_COMPRESSION OFF" "ENABLE_PUSH OFF" "ENABLE_TESTING OFF"
#)

CPMAddPackage(NAME bvh GITHUB_REPOSITORY madmann91/bvh)
#CPMAddPackage(
#    bvh
#    GIT_REPOSITORY https://github.com/madmann91/bvh.git
#    GIT_TAG        origin/master
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(NAME concurrentqueue GITHUB_REPOSITORY cameron314/concurrentqueue)
#FetchContent_Declare(
#    concurrentqueue
#    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
#    GIT_TAG        origin/master
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(NAME cpp-terminal GITHUB_REPOSITORY jupyter-xeus/cpp-terminal)
#FetchContent_Declare(
#    cpp-terminal
#    GIT_REPOSITORY https://github.com/jupyter-xeus/cpp-terminal.git
#    GIT_TAG        origin/master
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(NAME cxxopts GITHUB_REPOSITORY jarro2783/cxxopts)
#FetchContent_Declare(
#    cxxopts
#    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
#    GIT_TAG        origin/master
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(NAME embree VERSION 3.13.3 GITHUB_REPOSITORY embree/embree)
#FetchContent_Declare(
#    embree
#    GIT_REPOSITORY https://github.com/embree/embree.git
#    GIT_TAG        v3.13.3 # TODO v4.1.0 is out
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              etl
    VERSION           20.38.17
    GIT_TAG           20.38.17
    GITHUB_REPOSITORY ETLCPP/etl
)
#FetchContent_Declare(
#    etl
#    GIT_REPOSITORY https://github.com/ETLCPP/etl.git
#    GIT_TAG        20.38.17
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(NAME fastgltf GITHUB_REPOSITORY spnda/fastgltf)
#FetchContent_Declare(
#    fastgltf
#    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
#    GIT_TAG        main # TODO v0.8.0
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

#FetchContent_Declare(
#    flecs
#    GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
#    GIT_TAG        origin/master
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              fmt
    VERSION           11.0.2
    GIT_TAG           11.0.2
    GITHUB_REPOSITORY fmtlib/fmt
)
#FetchContent_Declare(
#    fmt
#    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
#    #GIT_TAG        origin/master
#    #GIT_TAG        171a020c828669490b98af0c2640e50f1511d2cc
#    GIT_TAG        11.0.2
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              freetype
    VERSION           2.13.2
    GIT_TAG           VER-2-13-2
    GITHUB_REPOSITORY freetype/freetype
)
#FetchContent_Declare(
#    freetype
#    #GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
#    GIT_REPOSITORY https://github.com/freetype/freetype.git
#    GIT_TAG        VER-2-13-2
#)

CPMAddPackage(
    NAME              geogram
    GIT_TAG           cmake-fetchcontent
    GITHUB_REPOSITORY tksuoran/geogram
    OPTIONS
        "GEOGRAM_WITH_GRAPHICS         OFF"
        "GEOGRAM_WITH_LEGACY_NUMERICS  OFF"
        "GEOGRAM_WITH_HLBFGS           ON"
        "GEOGRAM_WITH_TETGEN           ON"
        "GEOGRAM_WITH_TRIANGLE         ON"
        "GEOGRAM_WITH_LUA              OFF"
        "GEOGRAM_LIB_ONLY              ON"
        "GEOGRAM_WITH_FPG              ON"
        "GEOGRAM_USE_SYSTEM_GLFW3      OFF"
        "GEOGRAM_WITH_GARGANTUA        OFF"
        "GEOGRAM_WITH_TBB              OFF"
)
#FetchContent_Declare(
#    geogram
#    GIT_REPOSITORY https://github.com/tksuoran/geogram.git
#    GIT_TAG        cmake-fetchcontent
#    # GIT_REPOSITORY https://github.com/BrunoLevy/geogram.git
#    # GIT_TAG        v1.9.2
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

# Not in use yet
#FetchContent_Declare(
#    GeometricTools
#    GIT_REPOSITORY https://github.com/tksuoran/GeometricTools.git
#    GIT_TAG        cmake # GTE-version-5.14
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              glfw
    VERSION           3.4
    GIT_TAG           3.4
    GITHUB_REPOSITORY glfw/glfw
    OPTIONS
        "BUILD_SHARED_LIBS   OFF"
        "GLFW_BUILD_EXAMPLES OFF"
        "GLFW_BUILD_TESTS    OFF"
        "GLFW_BUILD_DOCS     OFF"
        "GLFW_INSTALL        OFF"
        "GLFW_VULKAN_STATIC  OFF"
)
#FetchContent_Declare(
#    glfw
#    GIT_REPOSITORY https://github.com/glfw/glfw.git
#    GIT_TAG        3.4
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(NAME glm GITHUB_REPOSITORY g-truc/glm)
#FetchContent_Declare(
#    glm
#    GIT_REPOSITORY https://github.com/g-truc/glm.git
#    GIT_TAG        origin/master
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(NAME glslang GITHUB_REPOSITORY KhronosGroup/glslang)
#FetchContent_Declare(
#    glslang
#    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
#    GIT_TAG        origin/master
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

#FetchContent_Declare(
#    glTF-SDK
#    GIT_REPOSITORY https://github.com/microsoft/glTF-SDK.git
#    GIT_TAG        master
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

# Not yet in use
#FetchContent_Declare(
#    googletest
#    GIT_REPOSITORY https://github.com/google/googletest.git
#    GIT_TAG        master
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              harfbuzz
    GIT_TAG           fix-bigobj
    GITHUB_REPOSITORY tksuoran/harfbuzz
)
#FetchContent_Declare(
#    harfbuzz
#    #GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
#    #GIT_TAG        8.4.0
#    GIT_REPOSITORY https://github.com/tksuoran/harfbuzz.git
#    GIT_TAG        fix-bigobj
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

#FetchContent_Declare(
#    ImGui
#    GIT_REPOSITORY https://github.com/ocornut/imgui.git
#    GIT_TAG        master # v1.81
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

# Not yet in use
#FetchContent_Declare(
#    libigl
#    GIT_REPOSITORY https://github.com/libigl/libigl.git
#    GIT_TAG        v2.3.0
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              JoltPhysics
    GITHUB_REPOSITORY jrouwe/JoltPhysics
    OPTIONS
        "USE_ASSERTS OFF"
        "DOUBLE_PRECISION OFF"
        "GENERATE_DEBUG_SYMBOLS ON"
        "OVERRIDE_CXX_FLAGS OFF"
        "CROSS_PLATFORM_DETERMINISTIC OFF"
        "CROSS_COMPILE_ARM OFF"
        "BUILD_SHARED_LIBS OFF"
        "INTERPROCEDURAL_OPTIMIZATION OFF" # Differs from Jolt
        "FLOATING_POINT_EXCEPTIONS_ENABLED ON"
        "CPP_RTTI_ENABLED ON"
        "OBJECT_LAYER_BITS 16"
        "USE_SSE4_1 ON"
        "USE_SSE4_2 ON"
        "USE_AVX ON"
        "USE_AVX2 ON"
        "USE_AVX512 OFF"
        "USE_LZCNT ON"
        "USE_TZCNT ON"
        "USE_F16C ON"
        "USE_FMADD ON"
        "USE_WASM_SIMD OFF"
        "ENABLE_ALL_WARNINGS ON"
        "TRACK_BROADPHASE_STATS OFF"
        "TRACK_NARROWPHASE_STATS OFF"
        "DEBUG_RENDERER_IN_DEBUG_AND_RELEASE ON"
        "DEBUG_RENDERER_IN_DISTRIBUTION OFF"
        "PROFILER_IN_DEBUG_AND_RELEASE ON"
        "PROFILER_IN_DISTRIBUTION OFF"
        "DISABLE_CUSTOM_ALLOCATOR OFF"
        "USE_STD_VECTOR OFF"
        "ENABLE_OBJECT_STREAM ON"
        "USE_STATIC_MSVC_RUNTIME_LIBRARY OFF"
        "ENABLE_INSTALL OFF"
)
#FetchContent_Declare(
#    JoltPhysics
#    GIT_REPOSITORY  https://github.com/jrouwe/JoltPhysics.git
#    GIT_TAG         master # v5.0.0 has issue
#    GIT_SHALLOW     TRUE
#    GIT_PROGRESS    TRUE
#)

CPMAddPackage(
    NAME              lunasvg
    VERSION           2.4.0
    GITHUB_REPOSITORY sammycage/lunasvg
)
#FetchContent_Declare(
#    lunasvg
#    GIT_REPOSITORY  https://github.com/sammycage/lunasvg.git
#    GIT_TAG         v2.4.0
#    GIT_SHALLOW     TRUE
#    GIT_PROGRESS    TRUE
#)

CPMAddPackage(
    NAME              MathGeoLib
    GIT_TAG           origin/winver
    GITHUB_REPOSITORY tksuoran/MathGeoLib
)
#FetchContent_Declare(
#    MathGeoLib
#    #GIT_REPOSITORY https://github.com/juj/MathGeoLib.git
#    GIT_REPOSITORY  https://github.com/tksuoran/MathGeoLib.git
#    GIT_TAG        origin/winver
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

#FetchContent_Declare(
#    meshoptimizer
#    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
#    GIT_TAG        v0.21
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              nlohmann_json
    VERSION           3.11.3
    GITHUB_REPOSITORY nlohmann/json
)
#FetchContent_Declare(
#    nlohmann_json
#    GIT_REPOSITORY https://github.com/nlohmann/json.git
#    GIT_TAG        v3.11.3
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              nvtx
    VERSION           3.1.0
    GITHUB_REPOSITORY NVIDIA/NVTX
)
#FetchContent_Declare(
#    nvtx
#    GIT_REPOSITORY https://github.com/NVIDIA/NVTX.git
#    GIT_TAG        v3.1.0
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              OpenXR-SDK
    VERSION           1.1.38
    GIT_TAG           release-1.1.38
    GITHUB_REPOSITORY KhronosGroup/OpenXR-SDK
)
#FetchContent_Declare(
#    OpenXR-SDK
#    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
#    #GIT_TAG        release-1.0.34
#    GIT_TAG        release-1.1.38
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              simdjson
    VERSION           3.9.4
    GITHUB_REPOSITORY simdjson/simdjson
)
#FetchContent_Declare(
#    simdjson
#    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
#    GIT_TAG        v3.9.4
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              spdlog
    VERSION           1.14.1
    GITHUB_REPOSITORY gabime/spdlog
    OPTIONS "SPDLOG_FMT_EXTERNAL ON"
)
#FetchContent_Declare(
#    spdlog
#    GIT_REPOSITORY https://github.com/gabime/spdlog.git
#    GIT_TAG        v1.14.1
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              taskflow
    VERSION           3.7.0
    GITHUB_REPOSITORY taskflow/taskflow
    OPTIONS
        "TF_BUILD_TESTS     OFF"
        "TF_BUILD_EXAMPLES  OFF"
)
#FetchContent_Declare(
#    taskflow
#    GIT_REPOSITORY https://github.com/taskflow/taskflow.git
#    GIT_TAG        v3.7.0
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              tracy
    VERSION           0.11.1
    GITHUB_REPOSITORY wolfpld/tracy
    OPTIONS 
)
#FetchContent_Declare(
#    tracy
#    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
#    GIT_TAG        v0.11.1
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

CPMAddPackage(
    NAME              wuffs
    VERSION           0.4.0-alpha.5
    GITHUB_REPOSITORY google/wuffs
)
#FetchContent_Declare(
#    wuffs
#    GIT_REPOSITORY https://github.com/google/wuffs.git
#    GIT_TAG        v0.4.0-alpha.5
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)
