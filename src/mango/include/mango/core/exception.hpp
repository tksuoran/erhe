/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <cstdarg>
#include <string>
#include <exception>
#include <mango/core/configure.hpp>
#include <mango/core/string.hpp>

namespace mango
{

	// ----------------------------------------------------------------------------
    // Status
	// ----------------------------------------------------------------------------

    struct Status
    {
        std::string info;
        bool success = true;

        operator bool () const;
        void setError(const std::string& error);
        void setError(const char* format, ...);
    };

	// ----------------------------------------------------------------------------
	// Exception
	// ----------------------------------------------------------------------------

    class Exception : public std::exception
    {
    protected:
        std::string m_message;
        std::string m_func;
        std::string m_file;
        int m_line;

    public:
        Exception(const std::string message, const std::string func, const std::string file, int line);
        ~Exception() noexcept;

        const char* what() const noexcept;
        const char* func() const;
        const char* file() const;
        int line() const;
    };

	// ----------------------------------------------------------------------------
	// MANGO_EXCEPTION(...)
	// ----------------------------------------------------------------------------

#ifdef MANGO_PLATFORM_WINDOWS
    #define MANGO_EXCEPTION(...) \
        throw mango::Exception(mango::makeString(__VA_ARGS__), __FUNCTION__, __FILE__, __LINE__)
#else
    #define MANGO_EXCEPTION(...) \
        throw mango::Exception(mango::makeString(__VA_ARGS__), __func__, __FILE__, __LINE__)
#endif

} // namespace mango
