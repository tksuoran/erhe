# nvtx has CMakeLists.txt in c subdirectory

function (FetchContent_MakeAvailable_nvtx)
    FetchContent_GetProperties(nvtx)
    string(TOLOWER "nvtx" lc_nvtx)
    if (NOT ${lc_nvtx}_POPULATED)
        FetchContent_Populate(nvtx)

        add_subdirectory(${${lc_nvtx}_SOURCE_DIR}/c ${${lc_nvtx}_BINARY_DIR})
    endif ()
endfunction ()
