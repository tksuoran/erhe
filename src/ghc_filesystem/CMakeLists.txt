add_library(
    ghc_filesystem INTERFACE
    #include/ghc/filesystem.hpp
    #include/ghc/fs_fwd.hpp
    #include/ghc/fs_impl.hpp
    #include/ghc/fs_std_fwd.hpp
    #include/ghc/fs_std_impl.hpp
    #include/ghc/fs_std.hpp
)

target_include_directories(ghc_filesystem INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
