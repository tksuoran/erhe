include(FetchContent)

# Not yet in use
FetchContent_Declare(
    avir
    #GIT_REPOSITORY https://github.com/avaneev/avir.git
    #GIT_TAG        master
    GIT_REPOSITORY  https://github.com/tksuoran/avir.git
    GIT_TAG         cmake
    GIT_SHALLOW     TRUE
    GIT_PROGRESS    TRUE
)

FetchContent_Declare(
    bullet3
    #GIT_REPOSITORY https://github.com/bulletphysics/bullet3.git
    GIT_REPOSITORY https://github.com/tksuoran/bullet3.git
    GIT_TAG        cmake-fetchcontent
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

# Not in use directly - is included in mango
FetchContent_Declare(
    concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    embree
    GIT_REPOSITORY https://github.com/embree/embree.git
    GIT_TAG        v3.13.1
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        8.0.1
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
    GIT_TAG        VER-2-11-0
)

FetchContent_Declare(
    GeometricTools
    GIT_REPOSITORY https://github.com/tksuoran/GeometricTools.git
    GIT_TAG        cmake # GTE-version-5.14
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.4
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

# Not yet in use
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        master
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
    harfbuzz
    GIT_REPOSITORY https://github.com/tksuoran/harfbuzz.git
    GIT_TAG        fixes
    #GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    #GIT_TAG        2.7.4
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

# Not yet in use
FetchContent_Declare(
    libigl
    GIT_REPOSITORY https://github.com/libigl/libigl.git
    GIT_TAG        v2.3.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    lunasvg
    GIT_REPOSITORY  https://github.com/sammycage/lunasvg.git
    GIT_TAG         master
    GIT_SHALLOW     TRUE
    GIT_PROGRESS    TRUE
)

FetchContent_Declare(
    nlohmann_json
    #GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    mango
    #GIT_REPOSITORY https://github.com/t0rakka/mango.git
    GIT_REPOSITORY https://github.com/tksuoran/mango.git
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

# Not yet in use
FetchContent_Declare(
    reactphysics3d
    GIT_REPOSITORY https://github.com/DanielChappuis/reactphysics3d.git
    GIT_TAG        v0.8.0
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

# Not yet in use
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.8.5
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
