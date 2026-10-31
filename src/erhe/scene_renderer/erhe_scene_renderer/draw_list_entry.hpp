#pragma once

#include "erhe_math/aabb.hpp"
#include "erhe_primitive/enums.hpp"

#include <cstddef>
#include <cstdint>

namespace erhe::scene { class Lod_data; }

namespace erhe::scene_renderer {

// Draw_list_entry::lod_table_index of an entry whose variant has no LOD
// chain (doc/plans/lod_gpu_selection.md section 3).
constexpr uint32_t lod_table_index_none{0xffffffffu};

// The packed effective Lod record of an entry (Draw_list_entry::lod_word,
// doc/plans/lod_gpu_selection.md section 3), the form the culling
// classification reads on the CPU lanes and in cull_classify.comp:
//   bits  0..1  mode (erhe::scene::Lod_mode: 0 automatic, 1 forced, 2 off)
//   bits  2..4  forced level (0..7)
//   bits  5..8  bias (signed, two's complement, -8..7)
//   bits  9..11 min level (0..7)
//   bits 12..14 max level (0..7)
//   bits 15..18 level count of the entry's chain (0: no chain, else 1..8)
// The levels and the bias are clamped into their fields; a max level below
// the min level counts as the min level, as in
// erhe::scene::get_forced_lod_level().
[[nodiscard]] auto make_lod_word(const erhe::scene::Lod_data& lod, std::size_t level_count) -> uint32_t;

// The fields of a lod_word (make_lod_word()), for diagnostics and tests.
class Lod_word_fields
{
public:
    uint32_t mode        {0};
    uint32_t forced_level{0};
    int32_t  bias        {0};
    uint32_t min_level   {0};
    uint32_t max_level   {0};
    uint32_t level_count {0};
};
[[nodiscard]] auto decode_lod_word(uint32_t lod_word) -> Lod_word_fields;

// One primitive inside one draw list (doc/erhe/draw_list_renderer.md
// R15/R16). Fixed-size value type; the hot path iterates a contiguous vector
// of these. Everything shared by the list lives in Draw_list_key; the
// per-primitive GPU record (node transform, material GPU slot, joint slot,
// lightmap scale/offset, ...) lives in the parallel
// Draw_list::primitive_records (doc/erhe/draw_list_performance_improvements.md),
// not here.
class Draw_list_entry
{
public:
    // Index into Draw_list_scene's object storage (stable; free-list with
    // generations). Cold data (the Mesh shared_ptr, create info) lives there.
    uint32_t         object_index        {0};
    // Index into Mesh::get_primitives() of the owning mesh.
    uint16_t         mesh_primitive_index{0};
    // The Buffer_mesh variant the draw parameters below were baked from.
    // Registration picks it; the per-draw record write must read the same one,
    // and a change of variant needs re-registration (base_vertex, the index
    // ranges and the quantization AABB are all variant-specific).
    //
    // Sized to fit the slot the old padding occupied, so the hot contiguous
    // entry vector does not grow (see the static_assert in the .cpp).
    erhe::primitive::Mesh_variant variant{erhe::primitive::Mesh_variant::original};
    // Mirrored Item_flags word of the owning mesh (R12a); Item_filter is
    // evaluated against this at draw time (R7a).
    uint64_t         flag_bits           {0};
    // Indexed draw parameters for the list's primitive mode, baked at
    // registration (Buffer_mesh index_range + base_index / base_vertex).
    uint32_t         index_count         {0};
    uint32_t         first_index         {0};
    uint32_t         base_vertex         {0};
    // World-space bounds of the owning mesh, set at registration and by the
    // transform and bounds hooks, together with Draw_list::bounds (the SoA
    // copy the culling classifies). Kept as min / max because the shadow
    // bounds gather (Draw_list_scene::gather_shadow_bounds) hands the exact
    // box to the shadow fit: center +- extent does not round-trip it bit
    // for bit. Not maintained for skinned objects, which are never culled.
    erhe::math::Aabb world_aabb          {};
    // Level of detail (doc/plans/lod_gpu_selection.md section 3): the first
    // row of the entry's chain in the Draw_list_scene's level table
    // (lod_table_index_none without a chain), and the packed effective Lod
    // record with the chain's level count (make_lod_word()). Written at
    // registration; an effective-record change rewrites lod_word.
    uint32_t         lod_table_index     {lod_table_index_none};
    uint32_t         lod_word            {0};
};

} // namespace erhe::scene_renderer
