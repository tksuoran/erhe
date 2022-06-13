/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2019 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <mango/core/dynamic_library.hpp>
#include <mango/core/exception.hpp>
#include <mango/core/string.hpp>

namespace mango
{

    // ----------------------------------------------------------------------------
    // DynamicLibraryHandle
    // ----------------------------------------------------------------------------

    struct DynamicLibraryHandle
    {
        HMODULE handle;

        DynamicLibraryHandle(const std::string& filename)
        {
            u32 mode = ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
            handle = ::LoadLibraryW(u16_fromBytes(filename).c_str());
            ::SetErrorMode(mode);
            if (!handle)
            {
                MANGO_EXCEPTION("[DynamicLibrary] WIN32 LoadLibrary() failed.");
            }
        }

        ~DynamicLibraryHandle()
        {
            ::FreeLibrary(handle);
        }

        void* address(const std::string& symbol) const
        {
            void* ptr = ::GetProcAddress(handle, symbol.c_str());
            return ptr;
        }
    };

    // ----------------------------------------------------------------------------
    // DynamicLibrary
    // ----------------------------------------------------------------------------

    DynamicLibrary::DynamicLibrary(const std::string& filename)
    {
        m_handle = new DynamicLibraryHandle(filename);
    }

    DynamicLibrary::~DynamicLibrary()
    {
        delete m_handle;
    }

    void* DynamicLibrary::address(const std::string& symbol) const
    {
        return m_handle->address(symbol);
    }

} // namespace mango
