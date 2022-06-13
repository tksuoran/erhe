/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <string>
#include <mango/core/configure.hpp>
#include <mango/core/memory.hpp>

namespace mango
{

    class DynamicLibrary : protected NonCopyable
    {
    protected:
        struct DynamicLibraryHandle* m_handle;

    public:
        DynamicLibrary(const std::string& filename);
        ~DynamicLibrary();

        void* address(const std::string& symbol) const;
    };

} // namespace mango
