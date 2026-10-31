#include "erhe_scene_renderer/draw_list_entry.hpp"

#include "erhe_scene/lod.hpp"

#include <algorithm>

namespace erhe::scene_renderer {

static_assert(sizeof(Draw_list_entry) <= 64, "Draw_list_entry should stay within one cache line");

auto make_lod_word(const erhe::scene::Lod_data& lod, const std::size_t level_count) -> uint32_t
{
    const int      min_level    = std::clamp(lod.min_level, 0, 7);
    const int      max_level    = std::clamp(std::max(lod.max_level, min_level), 0, 7);
    const int      forced_level = std::clamp(lod.forced_level, 0, 7);
    const int      bias         = std::clamp(lod.bias, -8, 7);
    const uint32_t mode         = static_cast<uint32_t>(lod.mode) & 0x3u;
    const uint32_t count        = static_cast<uint32_t>(std::min(level_count, std::size_t{15}));
    return
        (mode                                 <<  0u) |
        (static_cast<uint32_t>(forced_level)  <<  2u) |
        ((static_cast<uint32_t>(bias) & 0xfu) <<  5u) |
        (static_cast<uint32_t>(min_level)     <<  9u) |
        (static_cast<uint32_t>(max_level)     << 12u) |
        (count                                << 15u);
}

auto decode_lod_word(const uint32_t lod_word) -> Lod_word_fields
{
    const uint32_t bias_bits = (lod_word >> 5u) & 0xfu;
    return Lod_word_fields{
        .mode         = (lod_word >>  0u) & 0x3u,
        .forced_level = (lod_word >>  2u) & 0x7u,
        .bias         = ((bias_bits & 0x8u) != 0u) ? (static_cast<int32_t>(bias_bits) - 16) : static_cast<int32_t>(bias_bits),
        .min_level    = (lod_word >>  9u) & 0x7u,
        .max_level    = (lod_word >> 12u) & 0x7u,
        .level_count  = (lod_word >> 15u) & 0xfu
    };
}

} // namespace erhe::scene_renderer
