include(CheckCXXCompilerFlag)

add_compile_options(-Wall;-Wextra;-Wno-unused;-Wno-unknown-pragmas;-Wno-sign-compare;-Wwrite-strings;-Wno-unused;-Wno-narrowing)
check_cxx_compiler_flag(-Wno-unqualified-std-cast-call ERHE_CXX_HAS_NO_UNQUALIFIED_STD_CAST_CALL_FLAG)
if (ERHE_CXX_HAS_NO_UNQUALIFIED_STD_CAST_CALL_FLAG)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-copy>")
endif ()
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>")
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

set(ERHE_ADDITIONAL_GL_INCLUDES "${PROJECT_SOURCE_DIR}/src/khronos/khronos")

# x86 SIMD intrinsics baseline (Intel Macs). fpng's accelerated CRC32 uses
# _mm_clmulepi64_si128 (PCLMULQDQ) plus SSE4.1 intrinsics; on Clang these
# always_inline intrinsics fail to compile ("always_inline ... target specific option
# mismatch") unless the matching -m flags are enabled. Set globally for x86 here, in
# the compiler-specific toolchain file, so it stays orthogonal to the physics backend.
# Not applied on Apple Silicon (arm64), where the flags are invalid and fpng compiles
# no SSE path.
if (("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64") OR ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "AMD64"))
    add_compile_options(-msse4.1 -mpclmul)
endif ()

function (erhe_target_settings_toolchain target)
    set_target_properties(${target} PROPERTIES XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
endfunction()
