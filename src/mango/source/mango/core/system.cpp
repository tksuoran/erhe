/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <mango/core/system.hpp>
#include <mango/core/cpuinfo.hpp>
#include <mango/core/thread.hpp>
#include <mango/simd/simd.hpp>
#include <sstream>

namespace mango
{

	// ----------------------------------------------------------------------------
	// getSystemInfo()
	// ----------------------------------------------------------------------------

    std::string getPlatformInfo()
    {
        std::stringstream info;

        info << "Platform: \"" << MANGO_PLATFORM_NAME << "\"" << std::endl;

        info << "CPU: \"" << MANGO_CPU_NAME << "\", ";
#ifdef MANGO_CPU_64BIT
        info << "Bits: 64, ";
#else
        info << "Bits: 32, ";
#endif
#ifdef MANGO_LITTLE_ENDIAN
        info << "Endian: LITTLE ";
#else
        info << "Endian: BIG ";
#endif
        info << std::endl;

        return info.str();
    }

    std::string getSystemInfo()
    {
        std::stringstream info;
        info << getPlatformInfo();

        u64 flags = getCPUFlags();

        info << "Detected CPU Features: ";
        if (!flags) info << "N/A";
        if (flags & INTEL_MMX) info << "MMX ";
        if (flags & INTEL_MMX_PLUS) info << "MMX+ ";
        if (flags & INTEL_SSE) info << "SSE ";
        if (flags & INTEL_SSE2) info << "SSE2 ";
        if (flags & INTEL_SSE3) info << "SSE3 ";
        if (flags & INTEL_SSSE3) info << "SSSE3 ";
        if (flags & INTEL_SSE4_1) info << "SSE4.1 ";
        if (flags & INTEL_SSE4_2) info << "SSE4.2 ";
        if (flags & INTEL_SSE4A) info << "SSE4A ";
        if (flags & INTEL_XOP) info << "XOP ";
        if (flags & INTEL_3DNOW) info << "3DNOW ";
        if (flags & INTEL_3DNOW_EXT) info << "3DNOW+ ";
        if (flags & INTEL_AVX) info << "AVX ";
        if (flags & INTEL_AVX2) info << "AVX2 ";
        if (flags & INTEL_AES) info << "AES ";
        if (flags & INTEL_CLMUL) info << "CLMUL ";
        if (flags & INTEL_FMA3) info << "FMA3 ";
        if (flags & INTEL_MOVBE) info << "MOVBE ";
        if (flags & INTEL_POPCNT) info << "POPCNT ";
        if (flags & INTEL_F16C) info << "F16C ";
        if (flags & INTEL_RDRAND) info << "RDRAND ";
        if (flags & INTEL_CMOV) info << "CMOV ";
        if (flags & INTEL_CMPXCHG16B) info << "CMPXCHG16B ";
        if (flags & INTEL_FMA4) info << "FMA4 ";
        if (flags & INTEL_BMI1) info << "BMI1 ";
        if (flags & INTEL_BMI2) info << "BMI2 ";
        if (flags & INTEL_SHA) info << "SHA ";
        if (flags & INTEL_LZCNT) info << "LZCNT ";
        if (flags & INTEL_AVX512F) info << "AVX512F ";
        if (flags & INTEL_AVX512PFI) info << "AVX512PFI ";
        if (flags & INTEL_AVX512ERI) info << "AVX512ERI ";
        if (flags & INTEL_AVX512CDI) info << "AVX512CDI ";
        if (flags & INTEL_AVX512BW) info << "AVX512BW ";
        if (flags & INTEL_AVX512VL) info << "AVX512VL ";
        if (flags & INTEL_AVX512DQ) info << "AVX512DQ ";
        if (flags & INTEL_AVX512IFMA) info << "AVX512IFMA ";
        if (flags & INTEL_AVX512VBMI) info << "AVX512VBMI ";
        if (flags & INTEL_AVX512IFMA) info << "AVX512FP16 ";
        if (flags & ARM_NEON) info << "NEON ";
        if (flags & ARM_CRC32) info << "CRC32 ";
        if (flags & ARM_AES) info << "AES ";
        if (flags & ARM_SHA1) info << "SHA1 ";
        if (flags & ARM_SHA2) info << "SHA2 ";
        if (flags & ARM_PMULL) info << "PMULL ";
        info << std::endl;

        // NOTE: These are flags used to compile the mango library and have no relevance to
        //       compiler settings used in the client application code. 
        info << "Compiled SIMD Features: ";

#if defined(MANGO_ENABLE_SIMD)

    #if defined(MANGO_ENABLE_SSE)
        info << "SSE ";
    #endif

    #if defined(MANGO_ENABLE_SSE2)
        info << "SSE2 ";
    #endif

    #if defined(MANGO_ENABLE_SSE3)
        info << "SSE3 ";
    #endif

    #if defined(MANGO_ENABLE_SSSE3)
        info << "SSSE3 ";
    #endif

    #if defined(MANGO_ENABLE_SSE4_1)
        info << "SSE4.1 ";
    #endif

    #if defined(MANGO_ENABLE_SSE4_2)
        info << "SSE4.2 ";
    #endif

    #if defined(MANGO_ENABLE_AVX)
        info << "AVX ";
    #endif

    #if defined(MANGO_ENABLE_AVX2)
        info << "AVX2 ";
    #endif

    #if defined(MANGO_ENABLE_AVX512)
        info << "AVX512 ";
    #endif

    #if defined(MANGO_ENABLE_XOP)
        info << "XOP ";
    #endif

    #if defined(MANGO_ENABLE_FMA4)
        info << "FMA4 ";
    #endif

    #if defined(MANGO_ENABLE_FMA3)
        info << "FMA3 ";
    #endif

    #if defined(MANGO_ENABLE_F16C)
        info << "F16C ";
    #endif

    #if defined(MANGO_ENABLE_BMI)
        info << "BMI ";
    #endif

    #if defined(MANGO_ENABLE_BMI2)
        info << "BMI2 ";
    #endif

    #if defined(MANGO_ENABLE_LZCNT)
        info << "LZCNT ";
    #endif

    #if defined(MANGO_ENABLE_AES)
        info << "AES ";
    #endif

    #if defined(MANGO_ENABLE_SHA)
        info << "SHA ";
    #endif

    #if defined(MANGO_ENABLE_NEON)
        info << "NEON ";
    #endif

    #if defined(__ARM_FEATURE_CRC32)
        info << "CRC32 ";
    #endif

    #if defined(__ARM_FEATURE_CRYPTO)
        info << "CRYPTO ";
    #endif

#else

        info << "N/A";

#endif

        info << std::endl;

        info << "Hardware threads: " << std::thread::hardware_concurrency() << std::endl;
        info << "Build: " << __DATE__ << "  " << __TIME__ << std::endl;

        return info.str();
    }

	// ----------------------------------------------------------------------------
	// debugPrint()
	// ----------------------------------------------------------------------------

    static bool g_debug_print_enable = false;

    bool debugPrintIsEnable()
    {
        return g_debug_print_enable;
    }

    void debugPrintEnable(bool enable)
    {
        g_debug_print_enable = enable;
    }

    void debugPrint(const char* format, ...)
    {
        if (g_debug_print_enable)
        {
            va_list args;
            va_start(args, format);
            std::vprintf(format, args);
            std::fflush(stdout);
            va_end(args);
        }
    }

	// ----------------------------------------------------------------------------
    // Status
	// ----------------------------------------------------------------------------

    Status::operator bool () const
    {
        return success;
    }

    void Status::setError(const std::string& error)
    {
        info = error;
        success = false;
    }

    void Status::setError(const char* format, ...)
    {
        constexpr size_t max_length = 512;
        char buffer[max_length];

        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, max_length, format, args);
        va_end(args);

        info = buffer;
        success = false;
    }

	// ----------------------------------------------------------------------------
	// Exception
	// ----------------------------------------------------------------------------

    Exception::Exception(const std::string message, const std::string func, const std::string file, int line)
        : m_message(message)
        , m_func(func)
        , m_file(file)
        , m_line(line)
    {
    }

    Exception::~Exception() noexcept
    {
    }

    const char* Exception::what() const noexcept
    {
        return m_message.c_str();
    }

    const char* Exception::func() const
    {
        return m_func.c_str();
    }

    const char* Exception::file() const
    {
        return m_file.c_str();
    }

    int Exception::line() const
    {
        return m_line;
    }

} // namespace mango
