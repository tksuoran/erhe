/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2019 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <mango/core/dynamic_library.hpp>
#include <mango/core/exception.hpp>
#include <dlfcn.h>

namespace mango
{

    // ----------------------------------------------------------------------------
    // DynamicLibraryHandle
    // ----------------------------------------------------------------------------

    struct DynamicLibraryHandle
    {
        void* handle = nullptr;

        DynamicLibraryHandle(const std::string& filename)
        {
            handle = ::dlopen(filename.c_str(), RTLD_NOW);
            if (!handle)
            {
                MANGO_EXCEPTION("[DynamicLibrary] %s", ::dlerror());
            }
        }

        ~DynamicLibraryHandle()
        {
            if (handle)
            {
                ::dlclose(handle);
            }
        }

        void* address(const std::string& symbol) const
        {
            return ::dlsym(handle, symbol.c_str());
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
