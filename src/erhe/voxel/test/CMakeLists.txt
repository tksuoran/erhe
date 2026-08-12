CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_voxel_tests")
add_executable(${_target}
    main.cpp
    test_grid.cpp
    test_openvdb_smoke.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::voxel
        erhe::geometry
        erhe::log
        erhe::math
        openvdb
        GTest::gtest
)

if (MSVC)
    # OpenVDB's template-heavy headers overflow the default COFF section limit.
    target_compile_options(${_target} PRIVATE /bigobj)
endif ()

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
