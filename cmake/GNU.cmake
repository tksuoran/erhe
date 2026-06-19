# TODO restore warnings that can be restored without issues with Jolt
#add_compile_options(-Wall;-Wextra;-Wno-unused;-Wno-unknown-pragmas;-Wno-sign-compare;-Wwrite-strings;-Wno-unused;-Wno-narrowing;-Wno-ignored-qualifiers;-Wno-inline)
add_compile_definitions(JPH_NO_FORCE_INLINE=1)

add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>")
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-empty-body>")
add_compile_options("$<$<CONFIG:RELEASE>:-O3>")
add_compile_options("$<$<CONFIG:DEBUG>:-O0;-g3>")

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
endfunction()
