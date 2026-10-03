set(_target "editor_renderer_tests")
add_executable(${_target}
    # The editor sources under test are compiled directly into the test
    # executable: the editor itself is an executable, so there is no editor
    # library to link against. probe_grid.cpp and radiance_cascades_layout.cpp
    # are pure glm math over erhe::math::Aabb.
    ${CMAKE_CURRENT_SOURCE_DIR}/../probe_grid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../radiance_cascades_layout.cpp
    test_radiance_cascades_layout.cpp
)

# "renderers/radiance_cascades_layout.hpp" is reached relative to the editor source root.
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(${_target}
    PRIVATE
        erhe::math
        GTest::gtest_main
)

erhe_target_settings(${_target} "erhe/tests")

add_dependencies(erhe_tests ${_target})

include(GoogleTest)
gtest_discover_tests(${_target})
