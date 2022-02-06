add_library(
    optional_lite INTERFACE
    #include/nonstd/optional.hpp
)

target_include_directories(optional_lite INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
