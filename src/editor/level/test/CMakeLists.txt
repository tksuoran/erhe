set(_target "editor_level_tests")
add_executable(${_target}
    main.cpp
    kit_shape_rows_starter.cpp
    kit_shape_rows_build_13_16.cpp
    kit_shape_rows_build_17_20.cpp
    kit_shape_rows_build_21_24.cpp
    kit_shape_rows_extra_01_08.cpp
    kit_shape_rows_extra_09_16.cpp
    kit_shape_rows_extra_17_24.cpp
    kit_shape_rows_extra_25_32.cpp
    kit_shape_rows_extra_33_36.cpp
    kit_shape_rows_machine_01_08.cpp
    kit_shape_rows_machine_09_18.cpp
    kit_shape_rows_former_builds.cpp
    kit_shape_test_support.cpp
    # The editor sources under test are compiled directly into the test
    # executable: the editor itself is an executable, so there is no editor
    # library to link against. The kit shape library needs erhe::geometry
    # (with its erhe::math headers), glm and fmt only; the region and height
    # map helpers need glm and erhe::math; the repeat expansion glm and fmt; the
    # group mirror glm and erhe::math; the stairs fit and mesh erhe::math; the
    # surface base frame glm and erhe::math; the surface options fmt; the kit
    # layout node values erhe::scene; the kit layout values and outlines
    # nlohmann_json; the piece record (the attachment) erhe::scene and nlohmann_json; the light frames glm and fmt; the snap to
    # surface search (surface_snap_search.hpp) is header-only; the surface attachment carry glm and erhe::math.
    # The foliage index needs glm and erhe::verify.
    # The kit item slot conversions need fmt and the Inventory_slot struct (header only) of the editor's config codegen.
    # The foliage sampler needs glm (with the kit random stream and the scatter pick); the foliage palette nlohmann_json and fmt.
    # The foliage system needs erhe::scene, erhe::utility, the piece record and the editor's log_scene (editor_log.cpp).
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_connection_flow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_item.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_item_slot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_connector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_layout.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_layout_connectors.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_layout_outline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_layout_values.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_leaf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_light.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_mesh_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_piece.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_replace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shape.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shape_checks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shape_library.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_architecture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_basic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_build_13_16.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_build_17_20.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_build_21_24.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_extra_01_08.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_extra_09_16.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_extra_17_24.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_extra_25_32.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_extra_33_36.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_machine_01_08.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_machine_09_18.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_classical.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_japanese.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_medieval.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_nature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_nature_plants.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_nature_terrain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_nature_rocks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_shapes_roof.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_surface_attachment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../level_group.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../level_region.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../level_repeat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../floor_polygon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../foliage_index.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../foliage_palette.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../foliage_sampler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../foliage_system.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../editor_log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kit_standard_property.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../stairs_fit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../stairs_mesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../surface_base_frame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../surface_options.cpp
    # Surface_options carries the codegen enums Surface_snap_rest and Surface_support (configure-time output of the editor's config codegen).
    ${_config_out}/surface_snap_rest.cpp
    ${_config_out}/surface_support.cpp
    test_kit_attachment.cpp
    test_kit_collision.cpp
    test_kit_connection_flow.cpp
    test_kit_connector.cpp
    test_kit_item_slot.cpp
    test_kit_layout.cpp
    test_kit_layout_connectors.cpp
    test_kit_layout_outline.cpp
    test_kit_layout_values.cpp
    test_kit_light.cpp
    test_kit_replace.cpp
    test_kit_shapes.cpp
    test_kit_shapes_architecture.cpp
    test_kit_shapes_basic.cpp
    test_kit_shapes_build_13_16.cpp
    test_kit_shapes_build_17_20.cpp
    test_kit_shapes_build_21_24.cpp
    test_kit_shapes_extra_01_08.cpp
    test_kit_shapes_extra_09_16.cpp
    test_kit_shapes_extra_17_24.cpp
    test_kit_shapes_extra_25_32.cpp
    test_kit_shapes_extra_33_36.cpp
    test_kit_shapes_machine_01_08.cpp
    test_kit_shapes_machine_09_18.cpp
    test_kit_shapes_medieval.cpp
    test_kit_shapes_nature.cpp
    test_kit_shapes_nature_plants.cpp
    test_kit_shapes_nature_rocks.cpp
    test_kit_surface_attachment.cpp
    test_level_group.cpp
    test_level_region.cpp
    test_level_repeat.cpp
    test_stairs_fit.cpp
    test_floor_polygon.cpp
    test_foliage_index.cpp
    test_foliage_palette.cpp
    test_foliage_sampler.cpp
    test_foliage_system.cpp
    test_stairs_mesh.cpp
    test_surface_base_frame.cpp
    test_surface_drag_anchor.cpp
    test_surface_snap_search.cpp
)

# "level/kit_shape.hpp" is reached relative to the editor source root, "config/generated/..." relative to its build root.
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../.. ${_config_out}/../..)
erhe_codegen_source_settings(${_config_out}/surface_snap_rest.cpp ${_config_out}/surface_support.cpp)

target_link_libraries(${_target}
    PRIVATE
        erhe::codegen
        erhe::geometry
        erhe::log
        erhe::math
        erhe::profile
        erhe::scene
        erhe::utility
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
