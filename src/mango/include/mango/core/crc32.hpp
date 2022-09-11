/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2022 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/core/configure.hpp>
#include <mango/core/memory.hpp>

namespace mango
{

    // Hardware acceleration support:
    // ---------------------------------------------------
    //                   crc32   crc32c
    // ---------------------------------------------------
    // ARM CRC32          x       x
    // Intel PCLMUL       x       -
    // Intel SSE4.2       -       x

    // NOTE: Initial crc default value is 0x0

    // CRC32
    u32 crc32(u32 crc, ConstMemory memory);
    u32 crc32_combine(u32 crc0, u32 crc1, size_t length1);

    // Castagnoli CRC32
    u32 crc32c(u32 crc, ConstMemory memory);
    u32 crc32c_combine(u32 crc0, u32 crc1, size_t length1);

} // namespace mango
