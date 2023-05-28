function (FetchContent_MakeAvailable_spdlog)
    option(SPDLOG_FMT_EXTERNAL "Use external fmt library instead of bundled" ON)
    FetchContent_MakeAvailable(spdlog)
endfunction()
