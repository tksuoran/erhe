# Mango has CMakeLists.txt in build subdirectory

function (FetchContent_MakeAvailable_glfw)
    FetchContent_GetProperties(glfw)
    string(TOLOWER "glfw" lc_glfw)
    if (NOT ${lc_glfw}_POPULATED)
        FetchContent_Populate(glfw)

        # Disable features not needed
        option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
        option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
        option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
        option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
        option(GLFW_INSTALL "Generate installation target" OFF)
        option(GLFW_VULKAN_STATIC "Assume the Vulkan loader is linked with the application" OFF)

        add_subdirectory(${${lc_glfw}_SOURCE_DIR} ${${lc_glfw}_BINARY_DIR})
    endif ()
endfunction ()
