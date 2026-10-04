add_compile_definitions(JPH_NO_FORCE_INLINE=1)

# erhe's warning set, applied per erhe target in erhe_target_settings_toolchain
# below (as cmake/Clang.cmake and cmake/msvc.cmake do) so that the CPM
# dependencies - Jolt among them, which once kept this set global and
# disabled - build with their own warning defaults.
set(ERHE_GNU_WARNING_FLAGS -Wall;-Wextra;-Wno-unused;-Wno-unknown-pragmas;-Wno-sign-compare;-Wwrite-strings;-Wno-narrowing;-Wno-ignored-qualifiers;-Woverloaded-virtual)

add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>")
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-empty-body>")
add_compile_options("$<$<CONFIG:RELEASE>:-O3>")
add_compile_options("$<$<CONFIG:DEBUG>:-O0;-g3>")

# ERHE_USE_ASAN / ERHE_USE_UBSAN (doc/building.md): AddressSanitizer and
# UndefinedBehaviorSanitizer for every target, including the CPM
# dependencies configured in this tree (the flag choice is cmake/Clang.cmake's).
if (ERHE_USE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif ()
if (ERHE_USE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=undefined)
endif ()

# Workaround for https://github.com/BrunoLevy/geogram/issues/257
add_compile_options("$<$<COMPILE_LANGUAGE:C>:-std=gnu99>")

# Workaround for MinGW linker (ld) is failing due to relocation overflows
if (WIN32)
    add_compile_options(-Wa,-mbig-obj)
endif ()

# x86 SIMD intrinsics baseline. fpng's accelerated CRC32 uses _mm_clmulepi64_si128
# (PCLMULQDQ) plus SSE4.1 intrinsics; on GCC these always_inline intrinsics fail to
# compile ("inlining failed in call to always_inline ... target specific option
# mismatch") unless the matching -m flags are enabled. Set globally for x86 here, in
# the compiler-specific toolchain file, so it stays orthogonal to the physics backend
# (it must NOT live in JoltPhysicsCompatibility.cmake, which is skipped for non-Jolt
# builds). Not applied on aarch64, where the flags are invalid and fpng compiles no
# SSE path.
if (("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64") OR ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "AMD64"))
    add_compile_options(-msse4.1 -mpclmul)
endif ()

function (erhe_target_settings_toolchain target)
    # C++-only: erhe targets may contain vendored C sources (e.g. wuffs),
    # and -Woverloaded-virtual is invalid for C.
    foreach (erhe_warning_flag IN LISTS ERHE_GNU_WARNING_FLAGS)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${erhe_warning_flag}>")
    endforeach ()
    # ERHE_WARNINGS_AS_ERRORS: per erhe target, as cmake/msvc.cmake's /WX.
    if (ERHE_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Werror>")
    endif ()
endfunction()
