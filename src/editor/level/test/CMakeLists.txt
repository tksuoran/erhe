set(_target "editor_level_tests")
add_executable(${_target}
    main.cpp
    # The editor sources under test are compiled directly into the test
    # executable: the editor itself is an executable, so there is no editor
    # library to link against. The kit shape library needs erhe::geometry
    # (with its erhe::math headers), glm and fmt only.
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_mesh_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shape.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shape_checks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shape_library.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_architecture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_basic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_nature.cpp
    test_kit_shapes.cpp
)

# "level/kit_shape.hpp" is reached relative to the editor source root.
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(${_target}
    PRIVATE
        erhe::geometry
        erhe::log
        erhe::math
        erhe::profile
        fmt::fmt
        glm::glm-header-only
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

add_dependencies(erhe_tests ${_target})

include(GoogleTest)
gtest_discover_tests(${_target})
