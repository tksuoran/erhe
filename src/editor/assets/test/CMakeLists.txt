set(_target "editor_asset_tests")
add_executable(${_target}
    main.cpp
    # The editor source under test is compiled directly into the test
    # executable: the editor itself is an executable, so there is no editor
    # library to link against.
    ${CMAKE_CURRENT_SOURCE_DIR}/../asset_paths.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../editor_asset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../editor_asset_load.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../local_mesh_items.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../item_keys.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../prepared_asset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../prepared_status.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../editor_log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../pending_item_removals.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../operations/operation_stack_selection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../scene/world_space_validity.cpp
    test_editor_asset.cpp
    test_editor_asset_load.cpp
    test_pending_item_removals.cpp
    test_free_undone_loads.cpp
    test_item_keys.cpp
    test_prepared_asset.cpp
    test_prepared_status.cpp
    test_world_space_validity.cpp
)

# app_message.hpp (for Removed_items) is reached as "app_message.hpp", which
# only the editor target has on its include path.
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(${_target}
    PRIVATE
        erhe::asset_store
        erhe::dataformat
        erhe::item
        erhe::file
        erhe::geometry
        erhe::gltf
        erhe::graphics
        erhe::math
        erhe::primitive
        erhe::raytrace
        erhe::scene
        erhe::log
        erhe::verify
        GTest::gtest
        nlohmann_json::nlohmann_json
        simdjson::simdjson
        Taskflow
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})
