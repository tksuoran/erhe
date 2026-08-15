#pragma once

#include "erhe_scene_renderer/draw_list_key.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::scene_renderer {

// Registration input (doc/draw_list_renderer_requirements.md R1/R10a).
// Kept verbatim on the object so draw lists can be rebuilt from scratch
// (R1a) and so re-registration after mesh / material edits (R12) needs no
// caller-side state.
class Draw_list_object_create_info
{
public:
    std::shared_ptr<erhe::scene::Mesh> mesh;
    // static_ enables future static-list optimizations; dynamic is the
    // initial-scope default. Ignored (forced to skinned) when mesh->skin is
    // set.
    Draw_mobility                      mobility{Draw_mobility::dynamic};
};

// Handle to a registered object; index is stable (free-list), generation
// detects use after unregister.
class Draw_list_object_id
{
public:
    uint32_t index     {invalid_index};
    uint32_t generation{0};

    static constexpr uint32_t invalid_index = 0xffffffffu;

    [[nodiscard]] auto is_valid() const -> bool { return index != invalid_index; }
    [[nodiscard]] auto operator==(const Draw_list_object_id&) const -> bool = default;
};

// Where one entry of an object lives (R11): draw list index in
// Draw_list_scene::m_draw_lists and entry index in that list.
class Draw_list_entry_location
{
public:
    uint32_t draw_list_index{0};
    uint32_t entry_index    {0};
};

// One registered scene object (mesh) inside Draw_list_scene (R10/R11). Owns
// the mesh (and through it the primitives) for as long as it is registered.
class Draw_list_object
{
public:
    Draw_list_object_create_info          info;
    // Effective classification sampled at registration:
    Draw_mobility                         mobility            {Draw_mobility::dynamic};
    bool                                  negative_determinant{false}; // from node world transform (R10b)
    erhe::scene::Layer_id                 layer_id            {0};
    // Last mirrored Item_flags word (R12a).
    uint64_t                              flag_bits           {0};
    // Every entry belonging to this object, for O(entries of this object)
    // unregister / flag update / rebuild.
    std::vector<Draw_list_entry_location> locations;
    // Distinct materials the object's primitives referenced at registration
    // (raw pointers; kept alive through info.mesh). Used to maintain the
    // material identity watch (R12 material-content edits) and to find the
    // objects to re-register when a watched material changes.
    std::vector<const erhe::primitive::Material*> materials;
    // Node_transforms::world_from_node_serial the records were last written
    // from (transform hook dedup: several updates of one node in a frame
    // rewrite the object's records once).
    uint64_t                              transform_serial    {0};
    // Skin::skin_data.joint_buffer_index the records were last written from
    // (draw-time GPU-slot sync; skinned objects only).
    uint32_t                              joint_slot          {0};
    // Free-list bookkeeping.
    uint32_t                              generation          {0};
    bool                                  alive               {false};
};

} // namespace erhe::scene_renderer
