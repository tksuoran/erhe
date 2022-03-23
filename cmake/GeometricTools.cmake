# GeometricTools has CMakeLists.txt in GTE subdirectory

function (FetchContent_MakeAvailable_GeometricTools)
    FetchContent_GetProperties(GeometricTools)
    string(TOLOWER "GeometricTools" lc_GeometricTools)
    if (NOT ${lc_GeometricTools}_POPULATED)
        FetchContent_Populate(GeometricTools)
        add_subdirectory(${${lc_GeometricTools}_SOURCE_DIR}/GTE ${${lc_GeometricTools}_BINARY_DIR})
    endif ()
endfunction ()
