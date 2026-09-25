CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "editor_rig_tests")
add_executable(${_target}
    main.cpp
    # The editor sources under test are compiled directly into the test
    # executable: the editor itself is an executable, so there is no editor
    # library to link against. bone_naming.cpp is pure (std only);
    # bone_hierarchy.cpp needs erhe::scene (nodes and the bone flag);
    # bone_pose.cpp also the rest transforms (rig_properties.cpp, whose
    # Rig.tail default is bone_tail.cpp) and the channel-lock rule
    # (channel_locks.cpp); bone_mirror.cpp and bone_roll.cpp are pure glm
    # math over erhe::scene::Trs_transform; bone_bind.cpp the nearest-segment
    # rule and the rest world transforms over nodes and Rig.rest_* / tail.
    ${CMAKE_CURRENT_SOURCE_DIR}/../bone_bind.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../bone_hierarchy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../bone_mirror.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../bone_naming.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../bone_pose.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../bone_roll.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../bone_tail.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../scene/rig_properties.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../transform/channel_locks.cpp
    test_bone_bind.cpp
    test_bone_hierarchy.cpp
    test_bone_mirror.cpp
    test_bone_naming.cpp
    test_bone_pose.cpp
    test_bone_roll.cpp
    test_bone_tail.cpp
)

# "rig/bone_naming.hpp" is reached relative to the editor source root.
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(${_target}
    PRIVATE
        erhe::scene
        erhe::log
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

add_dependencies(erhe_tests ${_target})

include(GoogleTest)
gtest_discover_tests(${_target})
