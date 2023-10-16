function (FetchContent_MakeAvailable_igl)
    FetchContent_GetProperties(igl)
    string(TOLOWER "igl" lc_igl)
    if (NOT ${lc_igl}_POPULATED)
        FetchContent_Populate(igl)

        # Apply custom configuration
        option(IGL_WITH_SAMPLES  "Enable sample demo apps"       OFF)

        option(IGL_WITH_OPENGL   "Enable IGL/OpenGL"             OFF)
        option(IGL_WITH_OPENGLES "Enable IGL/OpenGL ES"          OFF)
        option(IGL_WITH_VULKAN   "Enable IGL/Vulkan"              ON)
        option(IGL_WITH_METAL    "Enable IGL/Metal"              OFF)
        option(IGL_WITH_WEBGL    "Enable IGL/WebGL"              OFF)
        
        option(IGL_WITH_IGLU     "Enable IGLU utils"             OFF)
        option(IGL_WITH_SHELL    "Enable Shell utils"            OFF)
        option(IGL_WITH_TESTS    "Enable IGL tests (gtest)"      OFF)
        option(IGL_WITH_TRACY    "Enable Tracy profiler"         OFF)
        option(IGL_ENFORCE_LOGS  "Enable logs in Release builds"  ON)
        
        option(IGL_DEPLOY_DEPS   "Deploy dependencies via CMake" OFF)
        
        add_subdirectory(${${lc_igl}_SOURCE_DIR} ${${lc_igl}_BINARY_DIR})
    endif ()
endfunction ()
