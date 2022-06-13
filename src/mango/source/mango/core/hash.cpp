/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <mango/core/hash.hpp>

#define XXH_INLINE_ALL
#include "../../external/xxhash/xxhash.h"

namespace mango
{

    u32 xxhash32(u32 seed, ConstMemory memory)
    {
        u32 hash = XXH32(memory.address, memory.size, seed);
        return hash;
    }

    u64 xxhash64(u64 seed, ConstMemory memory)
    {
        u64 hash = XXH64(memory.address, memory.size, seed);
        return hash;
    }

    u64 xx3hash64(u64 seed, ConstMemory memory)
    {
        u64 hash = XXH3_64bits_withSeed(memory.address, memory.size, seed);
        return hash;
    }

    XX3H128 xx3hash128(u64 seed, ConstMemory memory)
    {
        XXH128_hash_t x = XXH3_128bits_withSeed(memory.address, memory.size, seed);
        XX3H128 hash { x.low64, x.high64 };
        return hash;
    }

} // namespace mango
