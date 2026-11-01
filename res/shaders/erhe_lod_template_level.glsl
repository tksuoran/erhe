// The level of a draw-list entry's template range (doc/plans/
// lod_gpu_selection.md section 8), the port of
// erhe::scene_renderer::cull_lanes::get_lod_template_level() shared by
// cull_classify.comp and cull_compact.comp: under Lod_mode::forced the
// forced level clamped to the record's min / max and to the chain, else 0.
// lod_word is the entry's packed Lod record (make_lod_word()).

uint lod_template_level(uint lod_word)
{
    uint mode        = lod_word & 0x3u;
    uint level_count = (lod_word >> 15u) & 0xfu;
    if ((level_count == 0u) || (mode != 1u)) {
        return 0u;
    }
    uint forced_level = (lod_word >>  2u) & 0x7u;
    uint min_level    = (lod_word >>  9u) & 0x7u;
    uint max_level    = (lod_word >> 12u) & 0x7u; // make_lod_word() stored max_level >= min_level
    uint level        = min(max(forced_level, min_level), max_level);
    return min(level, level_count - 1u);
}
