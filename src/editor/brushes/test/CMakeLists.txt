CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "editor_brush_tests")
add_executable(${_target}
    main.cpp
    # The editor sources under test are compiled directly into the test
    # executable: the editor itself is an executable, so there is no editor
    # library to link against. brush_geometry_slot.cpp needs only
    # erhe::geometry and the editor brush logger, which editor_log.cpp
    # defines.
    ${CMAKE_CURRENT_SOURCE_DIR}/../brush_geometry_slot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../editor_log.cpp
    test_brush_geometry_slot.cpp
)

# "brushes/brush_geometry_slot.hpp" is reached relative to the editor source root.
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(${_target}
    PRIVATE
        erhe::geometry
        erhe::log
        erhe::math
        erhe::profile
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

add_dependencies(erhe_tests ${_target})

include(GoogleTest)
gtest_discover_tests(${_target})
