set(_target "editor_level_tests")
add_executable(${_target}
    main.cpp
    kit_shape_rows_originals.cpp
    kit_shape_rows_architecture_v2.cpp
    kit_shape_rows_basic_v2.cpp
    kit_shape_rows_build_13_16.cpp
    kit_shape_rows_build_17_20.cpp
    kit_shape_rows_build_21_24.cpp
    kit_shape_rows_medieval_v2.cpp
    kit_shape_rows_nature_plants_v2.cpp
    kit_shape_rows_nature_rocks_v2.cpp
    kit_shape_rows_nature_v2.cpp
    kit_shape_test_support.cpp
    # The editor sources under test are compiled directly into the test
    # executable: the editor itself is an executable, so there is no editor
    # library to link against. The kit shape library needs erhe::geometry
    # (with its erhe::math headers), glm and fmt only; the region and height
    # map helpers need glm and erhe::math; the repeat expansion glm and fmt; the
    # group mirror glm and erhe::math; the stairs fit and mesh erhe::math; the kit layout values and outlines nlohmann_json; the snap to
    # surface search (surface_snap_search.hpp) is header-only.
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_connector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_layout_connectors.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_layout_outline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_layout_values.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_leaf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_mesh_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_replace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shape.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shape_checks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shape_library.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_architecture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_architecture_v2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_basic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_basic_v2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_build_13_16.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_build_17_20.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_build_21_24.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_classical.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_japanese.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_medieval.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_medieval_v2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_nature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_nature_v2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_nature_plants.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_nature_plants_v2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_nature_terrain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_nature_rocks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_nature_rocks_v2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_roof.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../level_group.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../level_region.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../level_repeat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../stairs_fit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../stairs_mesh.cpp
    test_kit_connector.cpp
    test_kit_layout_connectors.cpp
    test_kit_layout_outline.cpp
    test_kit_layout_values.cpp
    test_kit_replace.cpp
    test_kit_shapes.cpp
    test_kit_shapes_architecture_v2.cpp
    test_kit_shapes_basic_v2.cpp
    test_kit_shapes_build_13_16.cpp
    test_kit_shapes_build_17_20.cpp
    test_kit_shapes_build_21_24.cpp
    test_kit_shapes_medieval_v2.cpp
    test_kit_shapes_nature_plants_v2.cpp
    test_kit_shapes_nature_rocks_v2.cpp
    test_kit_shapes_nature_v2.cpp
    test_level_group.cpp
    test_level_region.cpp
    test_level_repeat.cpp
    test_stairs_fit.cpp
    test_stairs_mesh.cpp
    test_surface_snap_search.cpp
)

# "level/kit_shape.hpp" is reached relative to the editor source root.
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(${_target}
    PRIVATE
        erhe::geometry
        erhe::log
        erhe::math
        erhe::profile
        erhe::verify
        fmt::fmt
        glm::glm-header-only
        GTest::gtest
        nlohmann_json::nlohmann_json
)

erhe_target_settings(${_target} "erhe/tests")

add_dependencies(erhe_tests ${_target})

include(GoogleTest)
gtest_discover_tests(${_target})
