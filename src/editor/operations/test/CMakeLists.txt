set(_target "editor_operation_tests")
add_executable(${_target}
    main.cpp
    # The editor sources under test are compiled directly into the test
    # executable: the editor itself is an executable, so there is no editor
    # library to link against. compound_operation.cpp (its roll_back with a
    # refused Property_edit_operation child), property_edit_operation.cpp and the shared
    # write step item_property_apply.cpp are tested with the real
    # erhe::scene items and Property_write_recording; the editor functions
    # they call that need the whole editor (App_context::on_item_property_changed,
    # the scene lookup of is_item_reference_allowed, the asset usership of
    # adopt_reference_usership, Asset_reference, the bone follow-ups of
    # append_bone_connect_follow_ups) get minimal test
    # definitions in editor_glue.cpp.
    ${CMAKE_CURRENT_SOURCE_DIR}/../compound_operation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../item_property_apply.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../property_edit_operation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../operation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../editor_log.cpp
    editor_glue.cpp
    editor_glue.hpp
    test_migrated_property_edits.cpp
    test_property_edit_operation.cpp
)

target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(${_target}
    PRIVATE
        erhe::item
        erhe::log
        erhe::primitive
        erhe::property
        erhe::scene
        erhe::verify
        fmt::fmt
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

add_dependencies(erhe_tests ${_target})

include(GoogleTest)
gtest_discover_tests(${_target})
