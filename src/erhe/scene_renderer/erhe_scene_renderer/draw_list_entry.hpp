#pragma once

#include "erhe_math/aabb.hpp"

#include <cstdint>

namespace erhe::scene_renderer {

// One primitive inside one draw list (doc/draw_list_renderer_requirements.md
// R15/R16). Fixed-size value type; the hot path iterates a contiguous vector
// of these. Everything shared by the list lives in Draw_list_key; everything
// that varies per frame (node transform, material GPU slot, joint slot,
// lightmap scale/offset) is read live from the Mesh / Mesh_primitive at
// upload time through object_index (R8a, R12b) and is NOT stored here.
class Draw_list_entry
{
public:
    // Index into Draw_list_scene's object storage (stable; free-list with
    // generations). Cold data (the Mesh shared_ptr, create info) lives there.
    uint32_t         object_index        {0};
    // Index into Mesh::get_primitives() of the owning mesh.
    uint16_t         mesh_primitive_index{0};
    uint16_t         pad                 {0};
    // Mirrored Item_flags word of the owning mesh (R12a); Item_filter is
    // evaluated against this at draw time (R7a).
    uint64_t         flag_bits           {0};
    // Indexed draw parameters for the list's primitive mode, baked at
    // registration (Buffer_mesh index_range + base_index / base_vertex).
    uint32_t         index_count         {0};
    uint32_t         first_index         {0};
    uint32_t         base_vertex         {0};
    // World-space bounds at registration (Q6: culling is future work; stale
    // for dynamic objects, unused by the initial draw path).
    erhe::math::Aabb world_aabb          {};
};

} // namespace erhe::scene_renderer
