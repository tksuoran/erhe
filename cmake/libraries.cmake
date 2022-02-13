include(FetchContent)

FetchContent_Declare(
    bullet3
    #GIT_REPOSITORY https://github.com/bulletphysics/bullet3.git
    GIT_REPOSITORY https://github.com/tksuoran/bullet3.git
    GIT_TAG        cmake-fetchcontent
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

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
    embree
    GIT_REPOSITORY https://github.com/embree/embree.git
    GIT_TAG        v3.13.1
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        8.1.1
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
    GIT_TAG        VER-2-11-0
)

# Not in use yet
#FetchContent_Declare(
#    GeometricTools
#    GIT_REPOSITORY https://github.com/tksuoran/GeometricTools.git
#    GIT_TAG        cmake # GTE-version-5.14
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.6
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

FetchContent_Declare(
    GSL
    GIT_REPOSITORY https://github.com/microsoft/GSL.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    harfbuzz
    #GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    #GIT_TAG        main
    GIT_REPOSITORY https://github.com/tksuoran/harfbuzz.git
    GIT_TAG        cmake-fix
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

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

FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
