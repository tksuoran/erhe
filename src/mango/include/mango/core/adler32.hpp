/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/core/configure.hpp>
#include <mango/core/memory.hpp>

namespace mango
{

    // NOTE: Initial adler default value is 0xffffffff

    u32 adler32(u32 adler, ConstMemory memory);
    u32 adler32_combine(u32 adler0, u32 adler1, size_t length1);

} // namespace mango
