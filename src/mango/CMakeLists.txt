CMAKE_MINIMUM_REQUIRED(VERSION 3.5)
PROJECT(mango CXX C)

cmake_policy(SET CMP0076 NEW) # target_sources() command converts relative paths

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# compiler
# ------------------------------------------------------------------------------

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    set(COMPILER_MSVC 1)
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(COMPILER_CLANG 1)
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Intel")
    set(COMPILER_INTEL 1)
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
    set(COMPILER_GCC 1)
else()
    message(STATUS "Compiler is not supported!")
endif()

# ------------------------------------------------------------------------------
# processor
# ------------------------------------------------------------------------------

message(STATUS "Processor: ${CMAKE_SYSTEM_PROCESSOR}")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "amd64.*|x86_64.*|AMD64.*")
    set(X86_64 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i686.*|i386.*|x86.*|amd64.*|AMD64.*")
    set(X86 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64.*|AARCH64.*)")
    set(AARCH64 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm.*|ARM.*)")
    set(ARM 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "PPC64*|ppc64*|powerpc64*")
    set(PPC64 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "PPC*|ppc*|powerpc*")
    set(PPC 1)
else()
    message(STATUS "Processor is not supported!")
endif()

# ------------------------------------------------------------------------------
# build
# ------------------------------------------------------------------------------

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build: ${CMAKE_BUILD_TYPE}")

# ------------------------------------------------------------------------------
# options
# ------------------------------------------------------------------------------

OPTION(BUILD_SHARED_LIBS    "Build as shared library (so/dll/dylib)"    OFF)

OPTION(ENABLE_FAST_MATH     "Use relaxed-precision floating point"      OFF)
OPTION(ENABLE_SSE2          "Enable SSE2 instructions"                  OFF)
OPTION(ENABLE_SSE4          "Enable SSE4.1 and SSE4.2 instructions"     OFF)
OPTION(ENABLE_AVX           "Enable AVX instructions"                   OFF)
OPTION(ENABLE_AVX2          "Enable AVX2 instructions"                  OFF)
OPTION(ENABLE_AVX512        "Enable AVX-512 instructions"               OFF)
OPTION(ENABLE_NEON          "Enable ARM NEON instructions"              OFF)
OPTION(ENABLE_F16C          "Enable F16C conversion instructions"       OFF)
OPTION(ENABLE_BMI           "Enable BMI"                                OFF)
OPTION(ENABLE_BMI2          "Enable BMI2"                               OFF)
OPTION(ENABLE_FMA           "Enable FMA"                                OFF)

ADD_LIBRARY(mango)

# ------------------------------------------------------------------------------
# include directories
# ------------------------------------------------------------------------------

target_include_directories(mango PUBLIC "include/")

# ------------------------------------------------------------------------------
# source directories
# ------------------------------------------------------------------------------

target_sources(
    mango
    PUBLIC
    include/mango/mango.hpp
    include/mango/core/atomic.hpp
    include/mango/core/bits.hpp
    include/mango/core/buffer.hpp
    include/mango/core/configure.hpp
    include/mango/core/core.hpp
    include/mango/core/cpuinfo.hpp
    #include/mango/core/dynamic_library.hpp
    include/mango/core/endian.hpp
    include/mango/core/exception.hpp
    include/mango/core/half.hpp
    include/mango/core/memory.hpp
    include/mango/core/pointer.hpp
    include/mango/core/stream.hpp
    include/mango/core/string.hpp
    include/mango/core/system.hpp
    PRIVATE
    source/mango/core/buffer.cpp
    source/mango/core/cpuinfo.cpp
    source/mango/core/memory.cpp
    source/mango/core/string.cpp
    source/mango/core/system.cpp
    PUBLIC
    include/mango/filesystem/file.hpp
    include/mango/filesystem/fileobserver.hpp
    include/mango/filesystem/filesystem.hpp
    include/mango/filesystem/mapper.hpp
    include/mango/filesystem/path.hpp
    PRIVATE
    source/mango/filesystem/file.cpp
    source/mango/filesystem/indexer.hpp
    source/mango/filesystem/mapper.cpp
    source/mango/filesystem/path.cpp
)

if (WIN32)
    target_sources(
        mango
        PRIVATE
        #source/mango/core/win32/dynamic_library.cpp
        source/mango/filesystem/win32/file_observer.cpp
        source/mango/filesystem/win32/file_stream.cpp
        source/mango/filesystem/win32/mapper_file.cpp
    )
else()
    target_sources(
        mango
        PRIVATE
        #source/mango/core/unix/dynamic_library.cpp
        source/mango/filesystem/unix/file_observer.cpp
        source/mango/filesystem/unix/file_stream.cpp
        source/mango/filesystem/unix/mapper_file.cpp
    )
endif()

# ------------------------------------------------------------------------------
# libraries
# ------------------------------------------------------------------------------

find_package(Threads)
if (CMAKE_THREAD_LIBS_INIT)
    target_link_libraries(mango "${CMAKE_THREAD_LIBS_INIT}")
endif ()

# ------------------------------------------------------------------------------
# configuration
# ------------------------------------------------------------------------------

if (COMPILER_MSVC)
    target_compile_options(mango PUBLIC "/DUNICODE")

    if (ENABLE_FAST_MATH)
        target_compile_options(mango PUBLIC "/fp:fast")
    endif ()

    if (ENABLE_AVX512)
        message(STATUS "SIMD: AVX-512 (2015)")
        target_compile_definitions(mango PUBLIC "-D__AVX512F__" "-D__AVX512DQ__")
    elseif (ENABLE_AVX2)
        message(STATUS "SIMD: AVX2 (2013)")
        target_compile_options(mango PUBLIC "/arch:AVX2")
    elseif (ENABLE_AVX)
        message(STATUS "SIMD: AVX (2008)")
        target_compile_options(mango PUBLIC "/arch:AVX")
    endif ()
else ()
    set(CMAKE_CXX_FLAGS "-Wall")
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")

    target_compile_definitions(mango PUBLIC $<$<PLATFORM_ID:Windows>:UNICODE>)

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        target_compile_options(mango PUBLIC "-ftree-vectorize")
    endif ()

    if (ENABLE_FAST_MATH)
        target_compile_options(mango PUBLIC "-ffast-math")
    endif ()

    if ((ARM AND ENABLE_NEON) OR AARCH64)
        message(STATUS "SIMD: NEON")
        if (ARM)
            if (!APPLE)
                # The compiler for Apple Mx CPUs don't recognize these
                add_definitions (-mfpu=neon -mfloat-abi=hard)
            endif()
            set(CMAKE_CXX_FLAGS "-fpermissive -Wno-psabi")
        endif ()
    endif ()

    if (X86 OR X86_64)
        # enable AES (2008) by default
        ### target_compile_options(mango PUBLIC "-maes")

        # enable CLMUL (2008) by default
        #### target_compile_options(mango PUBLIC "-mpclmul")

        # half conversion instructions
        if (ENABLE_F16C)
            message(STATUS "F16C: half conversion enabled (2012)")
            target_compile_options(mango PUBLIC "-mf16c")
        endif ()

        # bit manipulation instruction set 1
        if (ENABLE_BMI)
            message(STATUS "BMI+LZCNT: enabled (2013)")
            target_compile_options(mango PUBLIC "-mbmi")
            target_compile_options(mango PUBLIC "-mlzcnt")
        endif ()

        # bit manipulation instruction set 2
        if (ENABLE_BMI2)
            message(STATUS "BMI2: enabled (2013)")
            target_compile_options(mango PUBLIC "-mbmi2")
        endif ()

        # fused multiply-add
        if (ENABLE_FMA)
            message(STATUS "FMA: enabled (2013)")
            target_compile_options(mango PUBLIC "-mfma")
        endif ()

        # enable only one (the most recent) SIMD extension
        if (ENABLE_AVX512)
            message(STATUS "SIMD: AVX-512 (2015)")
            target_compile_options(mango PUBLIC "-mavx512dq")
            target_compile_options(mango PUBLIC "-mavx512vl")
            target_compile_options(mango PUBLIC "-mavx512bw")
        elseif (ENABLE_AVX2)
            message(STATUS "SIMD: AVX2 (2013)")
            target_compile_options(mango PUBLIC "-mavx2")
        elseif (ENABLE_AVX)
            message(STATUS "SIMD: AVX (2008)")
            target_compile_options(mango PUBLIC "-mavx")
        elseif (ENABLE_SSE4)
            message(STATUS "SIMD: SSE4.2 (2006)")
            target_compile_options(mango PUBLIC "-msse4")
        elseif (ENABLE_SSE2)
            message(STATUS "SIMD: SSE2 (2001)")
            target_compile_options(mango PUBLIC "-msse2")
        endif ()
    endif ()
endif ()
