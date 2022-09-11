/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <string>
#include <mango/core/configure.hpp>
#include <mango/core/thread.hpp>
#include <mango/core/timer.hpp>

namespace mango
{
#if 0 // tksuoran disabled in erhe
    struct Context
    {
        mutable ThreadPool thread_pool;
        Timer timer;
        bool debug_print_enable;

        Context();
        ~Context();
    };

    const Context& getSystemContext();

    std::string getPlatformInfo();
    std::string getSystemInfo();

    bool debugPrintIsEnable();
    void debugPrintEnable(bool enable);
    void debugPrint(const char* format, ...);
#endif
} // namespace mango
