add_library(spng)
target_compile_definitions(spng PUBLIC SPNG_STATIC)
target_sources(
    spng
    PUBLIC
        spng.h
    PRIVATE
        miniz.c
        miniz.h
        spng.c
)

target_compile_definitions(spng PUBLIC SPNG_USE_MINIZ)
target_include_directories(spng PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
