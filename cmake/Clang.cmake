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

# ERHE_USE_ASAN / ERHE_USE_UBSAN (doc/building.md): AddressSanitizer and
# UndefinedBehaviorSanitizer for every target, including the CPM
# dependencies configured in this tree, with frame pointers and debug info
# so the reports carry symbolized stacks in every configuration. UBSan's
# default set only: implicit-conversion is left out because Tracy's
# moodycamel queue trips it, and recovery stays on so the CI job decides
# through UBSAN_OPTIONS=halt_on_error=1 whether a report fails the test.
if (ERHE_USE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif ()
if (ERHE_USE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=undefined)
endif ()

# Clang on Linux links its sanitizer runtime statically and into executables
# only, so a sanitized shared library leaves every __asan_* / __ubsan_*
# reference unresolved. Dependencies that build shared libraries and link
# them with -Wl,--no-undefined (geogram's libgeogram.so) then fail to link.
# GCC links its shared libasan / libubsan into shared libraries as well,
# which is why only the Clang sanitizer tree hit this. -shared-libsan selects
# the shared runtime for every link, executables and shared libraries alike,
# which also keeps one runtime instance per process. The runtime lives in
# Clang's resource directory, outside the loader's default path, so it is
# added to every binary's run path; -frtlib-add-rpath does not do this on
# distributions (Ubuntu) whose runtime directory is not the per-target one.
if ((ERHE_USE_ASAN OR ERHE_USE_UBSAN) AND (NOT WIN32) AND (NOT APPLE) AND (NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    if (ERHE_USE_ASAN)
        set(erhe_sanitizer_runtime_name "libclang_rt.asan-${CMAKE_SYSTEM_PROCESSOR}.so")
    else ()
        set(erhe_sanitizer_runtime_name "libclang_rt.ubsan_standalone-${CMAKE_SYSTEM_PROCESSOR}.so")
    endif ()
    execute_process(
        COMMAND         "${CMAKE_CXX_COMPILER}" "-print-file-name=${erhe_sanitizer_runtime_name}"
        OUTPUT_VARIABLE erhe_sanitizer_runtime_path
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (NOT IS_ABSOLUTE "${erhe_sanitizer_runtime_path}" OR NOT EXISTS "${erhe_sanitizer_runtime_path}")
        message(FATAL_ERROR
            "ERHE_USE_ASAN / ERHE_USE_UBSAN: ${CMAKE_CXX_COMPILER} has no shared sanitizer runtime "
            "${erhe_sanitizer_runtime_name} (got '${erhe_sanitizer_runtime_path}'). "
            "On Debian / Ubuntu install libclang-rt-<version>-dev.")
    endif ()
    get_filename_component(erhe_sanitizer_runtime_dir "${erhe_sanitizer_runtime_path}" DIRECTORY)
    add_link_options(-shared-libsan "-Wl,-rpath,${erhe_sanitizer_runtime_dir}")
endif ()

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

# MSVC STL vector algorithms, disabled for clang-cl only.
#
# <xutility> gates std::find / std::remove / std::count on
# _Vector_alg_in_find_is_safe_elem, whose first term is
#   #ifdef __clang__
#   _Is_same_and_builtin_trivially_equality_comparable = is_same_v<..> && __is_trivially_equality_comparable(_Elem)
#   #else
#   ... = false
#   #endif
# The trait is true for ANY trivially equality comparable type - but
# _Find_vectorized / _Remove_vectorized only implement sizeof 1, 2, 4 and 8 and
# end in `static_assert(false, "unexpected size")`. So std::find over a
# contiguous range of, say, a three-int struct compiles under cl.exe (which
# takes the hard-coded false) and fails under clang-cl. Seen on
# editor::Lightmap_tile_key (12 bytes) with MSVC 14.51.36231.
#
# This is an STL bug, not ours, and it would fire again for the next such type,
# so turn the vector algorithms off for this configuration rather than reshaping
# call sites around it. The clang-cl tree exists to feed clangd a native
# compile_commands.json, so the lost vectorization costs nothing that ships.
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_definitions(_USE_STD_VECTOR_ALGORITHMS=0)
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

