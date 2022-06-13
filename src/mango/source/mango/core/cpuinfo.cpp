/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <algorithm>
#include <mango/core/cpuinfo.hpp>

namespace
{
    using namespace mango;

#if defined(MANGO_CPU_INTEL)

    // ----------------------------------------------------------------------------
    // cpuid()
    // ----------------------------------------------------------------------------

#if defined(MANGO_PLATFORM_WINDOWS)

#include "intrin.h"

    void cpuid(int* info, int id)
    {
        __cpuid(info, id);
    }

#elif defined(MANGO_PLATFORM_UNIX)

#include "cpuid.h"

    void cpuid(int* info, int id)
    {
        unsigned int regs[4] = { 0, 0, 0, 0 };
        __cpuid_count(id, 0, regs[0], regs[1], regs[2], regs[3]);

        info[0] = regs[0];
        info[1] = regs[1];
        info[2] = regs[2];
        info[3] = regs[3];
    }

#else

    #error "cpuid() not implemented."

#endif

    // ----------------------------------------------------------------------------
    // getCPUFlagsInternal()
    // ----------------------------------------------------------------------------

    u64 getCPUFlagsInternal()
    {
        u64 flags = 0;

        int cpuInfo[4] = { 0, 0, 0, 0 };

        // Get CPU information
        cpuid(cpuInfo, 0);
        unsigned int nIds = cpuInfo[0];

        for (unsigned int i = 0; i <= nIds; ++i)
        {
            cpuid(cpuInfo, i);

            switch (i)
            {
                case 1:
                    // edx
                    if ((cpuInfo[3] & 0x00800000) != 0) flags |= INTEL_MMX;
                    if ((cpuInfo[3] & 0x02000000) != 0) flags |= INTEL_SSE;
                    if ((cpuInfo[3] & 0x04000000) != 0) flags |= INTEL_SSE2;
                    if ((cpuInfo[3] & 0x00008000) != 0) flags |= INTEL_CMOV;
                    if ((cpuInfo[3] & 0x00800000) != 0) flags |= INTEL_AVX512FP16;
                    // ecx
                    if ((cpuInfo[2] & 0x00000001) != 0) flags |= INTEL_SSE3;
                    if ((cpuInfo[2] & 0x00000200) != 0) flags |= INTEL_SSSE3;
                    if ((cpuInfo[2] & 0x00080000) != 0) flags |= INTEL_SSE4_1;
                    if ((cpuInfo[2] & 0x00100000) != 0) flags |= INTEL_SSE4_2;
                    if ((cpuInfo[2] & 0x10000000) != 0) flags |= INTEL_AVX;
                    if ((cpuInfo[2] & 0x02000000) != 0) flags |= INTEL_AES;
                    if ((cpuInfo[2] & 0x00000002) != 0) flags |= INTEL_CLMUL;
                    if ((cpuInfo[2] & 0x00001000) != 0) flags |= INTEL_FMA3;
                    if ((cpuInfo[2] & 0x00400000) != 0) flags |= INTEL_MOVBE;
                    if ((cpuInfo[2] & 0x00800000) != 0) flags |= INTEL_POPCNT;
                    if ((cpuInfo[2] & 0x20000000) != 0) flags |= INTEL_F16C;
                    if ((cpuInfo[2] & 0x40000000) != 0) flags |= INTEL_RDRAND;
                    if ((cpuInfo[2] & 0x00002000) != 0) flags |= INTEL_CMPXCHG16B;
                    break;
                case 7:
                    // ebx
                    if ((cpuInfo[1] & 0x00000002) != 0) flags |= INTEL_AVX512VBMI;
                    if ((cpuInfo[1] & 0x00000008) != 0) flags |= (INTEL_BMI1 | INTEL_LZCNT);
                    if ((cpuInfo[1] & 0x00000020) != 0) flags |= INTEL_AVX2;
                    if ((cpuInfo[1] & 0x00000100) != 0) flags |= INTEL_BMI2;
                    if ((cpuInfo[1] & 0x00010000) != 0) flags |= INTEL_AVX512F;
                    if ((cpuInfo[1] & 0x00020000) != 0) flags |= INTEL_AVX512DQ;
                    if ((cpuInfo[1] & 0x00200000) != 0) flags |= INTEL_AVX512IFMA;
                    if ((cpuInfo[1] & 0x04000000) != 0) flags |= INTEL_AVX512PFI;
                    if ((cpuInfo[1] & 0x08000000) != 0) flags |= INTEL_AVX512ERI;
                    if ((cpuInfo[1] & 0x10000000) != 0) flags |= INTEL_AVX512CDI;
                    if ((cpuInfo[1] & 0x20000000) != 0) flags |= INTEL_SHA;
                    if ((cpuInfo[1] & 0x40000000) != 0) flags |= INTEL_AVX512BW;
                    if ((cpuInfo[1] & 0x80000000) != 0) flags |= INTEL_AVX512VL;
                    break;
            }
        }

        // Get extended CPU information
        cpuid(cpuInfo, 0x80000000);
        unsigned int nExtIds = cpuInfo[0];

        for (unsigned int i = 0x80000000; i <= nExtIds; ++i)
        {
            if (i == 0x80000001)
            {
                cpuid(cpuInfo, i);

                // ecx
                if ((cpuInfo[2] & 0x00000020) != 0) flags |= (INTEL_POPCNT | INTEL_LZCNT); // ABM
                if ((cpuInfo[2] & 0x00000040) != 0) flags |= INTEL_SSE4A;
                if ((cpuInfo[2] & 0x00010000) != 0) flags |= INTEL_FMA4;
                if ((cpuInfo[2] & 0x00000800) != 0) flags |= INTEL_XOP;
                // edx
                if ((cpuInfo[3] & 0x00800000) != 0) flags |= INTEL_MMX;
                if ((cpuInfo[3] & 0x00400000) != 0) flags |= INTEL_MMX_PLUS;
                if ((cpuInfo[3] & 0x80000000) != 0) flags |= INTEL_3DNOW;
                if ((cpuInfo[3] & 0x40000000) != 0) flags |= INTEL_3DNOW_EXT;
            }
        }

        return flags;
    }

#elif defined(MANGO_CPU_ARM) && defined(MANGO_PLATFORM_ANDROID)

#include <cpu-features.h>

    u64 getCPUFlagsInternal()
    {
        u64 flags = 0;

        if (android_getCpuFamily() == ANDROID_CPU_FAMILY_ARM)
        {
            uint64_t features = android_getCpuFeatures();

            if ((features & ANDROID_CPU_ARM_FEATURE_NEON) != 0) flags |= ARM_NEON;
            if ((features & ANDROID_CPU_ARM_FEATURE_AES) != 0) flags |= ARM_AES;
            if ((features & ANDROID_CPU_ARM_FEATURE_SHA1) != 0) flags |= ARM_SHA1;
            if ((features & ANDROID_CPU_ARM_FEATURE_SHA2) != 0) flags |= ARM_SHA2;
            if ((features & ANDROID_CPU_ARM_FEATURE_CRC32) != 0) flags |= ARM_CRC32;
        }
        else if (android_getCpuFamily() == ANDROID_CPU_FAMILY_ARM64)
        {
            uint64_t features = android_getCpuFeatures();

            flags |= (ARM_NEON | ARM_FP16); // default for ARM64
            if ((features & ANDROID_CPU_ARM64_FEATURE_AES) != 0) flags |= ARM_AES;
            if ((features & ANDROID_CPU_ARM64_FEATURE_SHA1) != 0) flags |= ARM_SHA1;
            if ((features & ANDROID_CPU_ARM64_FEATURE_SHA2) != 0) flags |= ARM_SHA2;
            if ((features & ANDROID_CPU_ARM64_FEATURE_CRC32) != 0) flags |= ARM_CRC32;
        }

        return flags;
    }

#elif defined(MANGO_CPU_ARM) && defined(MANGO_PLATFORM_LINUX)

#include <sys/auxv.h>
#include <asm/hwcap.h>

#if defined(MANGO_CPU_64BIT)

    #define AARCH64_HWCAP_FP (1UL << 0)
    #define AARCH64_HWCAP_ASIMD (1UL << 1)
    #define AARCH64_HWCAP_EVTSTRM (1UL << 2)
    #define AARCH64_HWCAP_AES (1UL << 3)
    #define AARCH64_HWCAP_PMULL (1UL << 4)
    #define AARCH64_HWCAP_SHA1 (1UL << 5)
    #define AARCH64_HWCAP_SHA2 (1UL << 6)
    #define AARCH64_HWCAP_CRC32 (1UL << 7)
    #define AARCH64_HWCAP_ATOMICS (1UL << 8)
    #define AARCH64_HWCAP_FPHP (1UL << 9)
    #define AARCH64_HWCAP_ASIMDHP (1UL << 10)
    #define AARCH64_HWCAP_CPUID (1UL << 11)
    #define AARCH64_HWCAP_ASIMDRDM (1UL << 12)
    #define AARCH64_HWCAP_JSCVT (1UL << 13)
    #define AARCH64_HWCAP_FCMA (1UL << 14)
    #define AARCH64_HWCAP_LRCPC (1UL << 15)
    #define AARCH64_HWCAP_DCPOP (1UL << 16)
    #define AARCH64_HWCAP_SHA3 (1UL << 17)
    #define AARCH64_HWCAP_SM3 (1UL << 18)
    #define AARCH64_HWCAP_SM4 (1UL << 19)
    #define AARCH64_HWCAP_ASIMDDP (1UL << 20)
    #define AARCH64_HWCAP_SHA512 (1UL << 21)
    #define AARCH64_HWCAP_SVE (1UL << 22)
    #define AARCH64_HWCAP_ASIMDFHM (1UL << 23)
    #define AARCH64_HWCAP_DIT (1UL << 24)
    #define AARCH64_HWCAP_USCAT (1UL << 25)
    #define AARCH64_HWCAP_ILRCPC (1UL << 26)
    #define AARCH64_HWCAP_FLAGM (1UL << 27)
    #define AARCH64_HWCAP_SSBS (1UL << 28)
    #define AARCH64_HWCAP_SB (1UL << 29)
    #define AARCH64_HWCAP_PACA (1UL << 30)
    #define AARCH64_HWCAP_PACG (1UL << 31)

    u64 getCPUFlagsInternal()
    {
        u64 flags = ARM_FP16 | ARM_NEON; // defaults for ARM64

        long hwcaps = getauxval(AT_HWCAP);

        if (hwcaps & AARCH64_HWCAP_AES) flags |= ARM_AES;
        if (hwcaps & AARCH64_HWCAP_CRC32) flags |= ARM_CRC32;
        if (hwcaps & AARCH64_HWCAP_SHA1) flags |= ARM_SHA1;
        if (hwcaps & AARCH64_HWCAP_SHA2) flags |= ARM_SHA2;
        if (hwcaps & AARCH64_HWCAP_PMULL) flags |= ARM_PMULL;

        return flags;
    }

#else // MANGO_CPU_64BIT

    #define ARM_HWCAP_SWP (1UL << 0)
    #define ARM_HWCAP_HALF (1UL << 1)
    #define ARM_HWCAP_THUMB (1UL << 2)
    #define ARM_HWCAP_26BIT (1UL << 3)
    #define ARM_HWCAP_FAST_MULT (1UL << 4)
    #define ARM_HWCAP_FPA (1UL << 5)
    #define ARM_HWCAP_VFP (1UL << 6)
    #define ARM_HWCAP_EDSP (1UL << 7)
    #define ARM_HWCAP_JAVA (1UL << 8)
    #define ARM_HWCAP_IWMMXT (1UL << 9)
    #define ARM_HWCAP_CRUNCH (1UL << 10)
    #define ARM_HWCAP_THUMBEE (1UL << 11)
    #define ARM_HWCAP_NEON (1UL << 12)
    #define ARM_HWCAP_VFPV3 (1UL << 13)
    #define ARM_HWCAP_VFPV3D16 (1UL << 14)
    #define ARM_HWCAP_TLS (1UL << 15)
    #define ARM_HWCAP_VFPV4 (1UL << 16)
    #define ARM_HWCAP_IDIVA (1UL << 17)
    #define ARM_HWCAP_IDIVT (1UL << 18)
    #define ARM_HWCAP_VFPD32 (1UL << 19)
    #define ARM_HWCAP_LPAE (1UL << 20)
    #define ARM_HWCAP_EVTSTRM (1UL << 21)

    #define ARM_HWCAP2_AES (1UL << 0)
    #define ARM_HWCAP2_PMULL (1UL << 1)
    #define ARM_HWCAP2_SHA1 (1UL << 2)
    #define ARM_HWCAP2_SHA2 (1UL << 3)
    #define ARM_HWCAP2_CRC32 (1UL << 4)

    u64 getCPUFlagsInternal()
    {
        u64 flags = 0;

        long hwcaps = getauxval(AT_HWCAP);

        if (hwcaps & ARM_HWCAP_VFPV4) flags |= ARM_FP16;
        if (hwcaps & ARM_HWCAP_NEON) flags |= ARM_NEON;

        long hwcaps2 = getauxval(AT_HWCAP2);

        if (hwcaps2 & ARM_HWCAP2_AES) flags |= ARM_AES;
        if (hwcaps2 & ARM_HWCAP2_CRC32) flags |= ARM_CRC32;
        if (hwcaps2 & ARM_HWCAP2_SHA1) flags |= ARM_SHA1;
        if (hwcaps2 & ARM_HWCAP2_SHA2) flags |= ARM_SHA2;
        if (hwcaps2 & ARM_HWCAP2_PMULL) flags |= ARM_PMULL;

        return flags;
    }

#endif // MANGO_CPU_64BIT

#elif defined(MANGO_CPU_ARM)

    // generic ARM (most likely macOS / iOS)

    u64 getCPUFlagsInternal()
    {
        u64 flags = 0;

#if defined(MANGO_CPU_64BIT)
        flags |= ARM_FP16 | ARM_NEON; // defaults for ARM64
#endif

#ifdef MANGO_ENABLE_NEON
        flags |= ARM_NEON;
#endif

#ifdef MANGO_ENABLE_ARM_FP16
        flags |= ARM_FP16;
#endif

#if defined(__ARM_FEATURE_CRYPTO)
        flags |= ARM_AES;
        flags |= ARM_SHA1;
        flags |= ARM_SHA2;
        flags |= ARM_PMULL;
#endif

#ifdef __ARM_FEATURE_CRC32
        flags |= ARM_CRC32;
#endif

        return flags;
    }

#else

    u64 getCPUFlagsInternal()
    {
        return 0; // unsupported platform
    }

#endif

    // cache the flags
    static u64 g_cpu_flags = getCPUFlagsInternal();

} // namespace

namespace mango
{

    u64 getCPUFlags()
    {
        return g_cpu_flags;
    }

} // namespace mango
