include(CheckCXXCompilerFlag)

# clang-cl (the MSVC-frontend clang driver) resolves options against the MSVC
# option table before GNU -W flags, and "-Wall" matches MSVC "/Wall", which
# clang-cl maps to -Weverything. Forward the warning flags through /clang: on
# the MSVC frontend so every flag keeps its GNU-driver meaning; the GNU
# frontend takes them as-is. Only -Wall collides today, but prefixing the
# whole set prevents a future flag from silently colliding the same way.
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(ERHE_GNU_WARNING_FLAG_PREFIX "/clang:")
else ()
    set(ERHE_GNU_WARNING_FLAG_PREFIX "")
endif ()

# erhe's warning set. Applied per target in erhe_target_settings_toolchain
# below (mirroring cmake/msvc.cmake, which sets /W4 per target) so that CPM
# dependency targets build with their own warning defaults instead of erhe's.
set(ERHE_GNU_WARNING_FLAGS -Wall;-Wextra;-Wno-unused;-Wno-unknown-pragmas;-Wno-sign-compare;-Wwrite-strings;-Wno-narrowing;-Woverloaded-virtual)
check_cxx_compiler_flag(-Wno-unqualified-std-cast-call ERHE_CXX_HAS_NO_UNQUALIFIED_STD_CAST_CALL_FLAG)
if (ERHE_CXX_HAS_NO_UNQUALIFIED_STD_CAST_CALL_FLAG)
    list(APPEND ERHE_GNU_WARNING_FLAGS -Wno-deprecated-copy)
endif ()
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # A single shared PCH (erhe_pch) cannot match every consumer's macro set:
    # dependency targets export PUBLIC compile definitions (JPH_*, TRACY_*,
    # SIMDJSON_*, ...) that differ between erhe targets, and none of them
    # affect any header actually inside the PCH. cl.exe tolerates the same
    # divergence silently; silence clang-cl's per-TU macro-mismatch warning.
    list(APPEND ERHE_GNU_WARNING_FLAGS -Wno-clang-cl-pch)
endif ()
list(TRANSFORM ERHE_GNU_WARNING_FLAGS PREPEND "${ERHE_GNU_WARNING_FLAG_PREFIX}")
# Optimization / debug levels, GNU-style. clang-cl (the MSVC-ABI clang driver)
# rejects -g3 as an unknown argument -- normally just a -Wunknown-argument
# warning, but Jolt compiles with -Werror, which makes it fatal -- and it
# ignores -O0/-O3. Apply these only to the GNU-frontend clang driver; clang-cl
# uses CMake's MSVC debug/release defaults (/Od /Zi for Debug, /O2 for Release).
if (NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options("$<$<CONFIG:RELEASE>:-O3>")
    add_compile_options("$<$<CONFIG:DEBUG>:-O0;-g3>")
endif ()

# TODO For now, to enable sanitizers, uncomment lines here

#add_compile_options(-fsanitize=undefined)
#add_link_options(-fsanitize=undefined)

#add_compile_options(-fsanitize=address)
#add_link_options(-fsanitize=address)

# No implicit-conversion because Tracy uses moodycamel
#add_compile_options(-fsanitize=undefined,float-divide-by-zero,local-bounds -fno-sanitize-recover=all)
#add_link_options(-fsanitize=undefined,float-divide-by-zero,local-bounds -fno-sanitize-recover=all)

#add_compile_options(-fsanitize=thread)
#add_link_options(-fsanitize=thread)

if (WIN32)
    set(ERHE_ADDITIONAL_GL_INCLUDES "${PROJECT_SOURCE_DIR}/src/khronos/khronos")
endif ()

# x86 SIMD intrinsics baseline. fpng's accelerated CRC32 uses _mm_clmulepi64_si128
# (PCLMULQDQ) plus SSE4.1 intrinsics; on Clang these always_inline intrinsics fail to
# compile ("always_inline ... target specific option mismatch") unless the matching
# -m flags are enabled. Set globally for x86 here, in the compiler-specific toolchain
# file, so it stays orthogonal to the physics backend (it must NOT live in
# JoltPhysicsCompatibility.cmake, which is skipped for non-Jolt builds). Not applied
# on aarch64, where the flags are invalid and fpng compiles no SSE path.
if (("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64") OR ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "AMD64"))
    add_compile_options(-msse4.1 -mpclmul)

    # clang-cl shared-PCH consistency. Jolt exports an AVX2 baseline to its
    # consumers (Jolt/Jolt.cmake: target_compile_options(Jolt PUBLIC -mavx2 -mbmi
    # -mpopcnt -mlzcnt -mf16c)), so erhe targets that link Jolt are built with
    # those target features while erhe_pch (which does not link Jolt) is not.
    # clang requires a precompiled header and every consuming TU to share an
    # identical target-feature set, so a single shared PCH forces this baseline
    # to be global. Apply it to clang-cl only: GNU-frontend clang (Linux/macOS)
    # already gets the equivalent flags globally via JoltPhysicsCompatibility's
    # non-MSVC branch. The editor already requires AVX2 at runtime because Jolt
    # is compiled with it, so this raises no CPU requirement. Keep this set in
    # sync with Jolt's PUBLIC options.
    if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        add_compile_options(-mavx2 -mbmi -mpopcnt -mlzcnt -mf16c)
    endif ()
endif ()

function (erhe_target_settings_toolchain target)
    # C++-only: erhe targets may contain vendored C sources (e.g. wuffs),
    # and -Woverloaded-virtual / -Wno-deprecated-copy are invalid for C.
    foreach (erhe_warning_flag IN LISTS ERHE_GNU_WARNING_FLAGS)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${erhe_warning_flag}>")
    endforeach ()
    if (WIN32)
        target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>)
        target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:_CRT_SECURE_NO_WARNINGS>)
    endif ()
endfunction()

