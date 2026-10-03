set(_target "erhe_smoke")
add_executable(${_target}
    main.cpp
    ../item/test/hierarchy_smoke.cpp
    ../item/test/hierarchy_smoke.hpp
)

target_include_directories(${_target} PRIVATE ../item/test)

target_link_libraries(${_target}
    PRIVATE
        erhe::item
        erhe::file
        erhe::log
        erhe::verify
)

erhe_target_settings(${_target} "erhe/tests")

# A short randomized run under ctest; longer soak runs pass their own
# --duration (doc/erhe/smoke.md).
add_test(NAME erhe_smoke COMMAND ${_target} --duration 2)
add_dependencies(erhe_tests ${_target})
