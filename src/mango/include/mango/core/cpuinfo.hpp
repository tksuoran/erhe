/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/core/configure.hpp>

namespace mango
{

	// ----------------------------------------------------------------------------
	// getCPUFlags()
	// ----------------------------------------------------------------------------

    enum : u64
    {
        INTEL_MMX         = 0x0000000000000001,
        INTEL_MMX_PLUS    = 0x0000000000000002,
        INTEL_SSE         = 0x0000000000000004,
        INTEL_SSE2        = 0x0000000000000008,
        INTEL_SSE3        = 0x0000000000000010,
        INTEL_SSSE3       = 0x0000000000000020,
        INTEL_SSE4_1      = 0x0000000000000040,
        INTEL_SSE4_2      = 0x0000000000000080,
        INTEL_SSE4A       = 0x0000000000000100,
        INTEL_XOP         = 0x0000000000000200,
        INTEL_3DNOW       = 0x0000000000000400,
        INTEL_3DNOW_EXT   = 0x0000000000000800,
        INTEL_AVX         = 0x0000000000001000,
        INTEL_AVX2        = 0x0000000000002000,
        INTEL_AES         = 0x0000000000004000,
        INTEL_CLMUL       = 0x0000000000008000,
        INTEL_FMA3        = 0x0000000000010000,
        INTEL_MOVBE       = 0x0000000000020000,
        INTEL_POPCNT      = 0x0000000000040000,
        INTEL_F16C        = 0x0000000000080000,
        INTEL_RDRAND      = 0x0000000000100000,
        INTEL_CMOV        = 0x0000000000200000,
        INTEL_CMPXCHG16B  = 0x0000000000400000,
        INTEL_FMA4        = 0x0000000000800000,
        INTEL_BMI1        = 0x0000000001000000,
        INTEL_BMI2        = 0x0000000002000000,
        INTEL_SHA         = 0x0000000004000000,
        INTEL_LZCNT       = 0x0000000008000000,
        INTEL_AVX512F     = 0x0000000010000000,
        INTEL_AVX512PFI   = 0x0000000020000000,
        INTEL_AVX512ERI   = 0x0000000040000000,
        INTEL_AVX512CDI   = 0x0000000080000000,
        INTEL_AVX512BW    = 0x0000000100000000,
        INTEL_AVX512VL    = 0x0000000200000000,
        INTEL_AVX512DQ    = 0x0000000400000000,
        INTEL_AVX512IFMA  = 0x0000000800000000,
        INTEL_AVX512VBMI  = 0x0000001000000000,
        INTEL_AVX512FP16  = 0x0000002000000000,

        ARM_NEON          = 0x0001000000000000,
        ARM_AES           = 0x0002000000000000,
        ARM_SHA1          = 0x0004000000000000,
        ARM_SHA2          = 0x0008000000000000,
        ARM_CRC32         = 0x0010000000000000,
        ARM_FP16          = 0x0020000000000000,
        ARM_PMULL         = 0x0040000000000000,
    };

	u64 getCPUFlags();

} // namespace mango
