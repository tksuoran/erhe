CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "editor_ik_solver_tests")
add_executable(${_target}
    main.cpp
    # The editor sources under test are compiled directly into the test
    # executable: the editor itself is an executable, so there is no editor
    # library to link against. ik_solver.cpp is pure (glm + std only);
    # ik_properties.cpp and rig_properties.cpp (Ik.rest_rotation defaults to
    # Rig.rest_rotation) need erhe::scene (their registered properties), and
    # rig_properties.cpp the Rig.tail default (rig/bone_tail.cpp).
    ${CMAKE_CURRENT_SOURCE_DIR}/../ik_solver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../rig/bone_tail.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../scene/ik_properties.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../scene/rig_properties.cpp
    test_ik_properties.cpp
    test_ik_solver.cpp
)

# "transform/ik_solver.hpp" is reached relative to the editor source root.
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(${_target}
    PRIVATE
        erhe::scene
        erhe::log
        glm::glm-header-only
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

add_dependencies(erhe_tests ${_target})

include(GoogleTest)
gtest_discover_tests(${_target})
