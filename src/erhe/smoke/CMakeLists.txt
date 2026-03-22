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
