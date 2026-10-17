#pragma once

#include "erhe_scene_renderer/draw_list_entry.hpp"
#include "erhe_scene_renderer/draw_list_key.hpp"

#include "erhe_math/aabb.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace erhe::graphics {
    class Reloadable_shader_stages;
}

namespace erhe::scene_renderer {

// Shadow caster sub-variant (doc/erhe/draw_list_renderer.md R4a):
// which forced VARIANT bits the shadow pass draws with. Selected by the
// caller per shadow pass, exactly as Shadow_renderer chooses today.
enum class Shadow_sub_variant : uint8_t
{
    depth_only          = 0, // directional / spot, depth technique
    depth_only_distance = 1, // directional / spot, distance technique (VARIANT_SHADOW_DISTANCE)
    cube                = 2, // point light cube faces (VARIANT_SHADOW_CUBE)
    count               = 3
};

[[nodiscard]] auto c_str(Shadow_sub_variant sub_variant) -> const char*;

enum class Draw_blending_selection : uint8_t
{
    opaque_only      = 0,
    translucent_only = 1,
    both             = 2  // opaque lists first, then translucent (R7)
};

// One culling view of the frame being rendered
// (doc/erhe/draw_list_renderer.md Q6): the index
// Draw_list_scene::add_cull_view() handed out for the view's planes, or
// none: the pass draws every entry passing its filter (multiview passes,
// passes without planes).
enum class Cull_view : uint16_t
{
    none = 0xffffu
};

// The range sphere of a point light (Draw_list_scene::add_cull_view() of a
// cube face): the first test of the face's view, so an entry whose box lies
// wholly outside the sphere is outside the view whatever its planes say.
class Cull_sphere
{
public:
    glm::vec3 center{0.0f};
    float     radius{0.0f};
};

class Draw_statistics
{
public:
    std::size_t draw_list_count{0}; // lists that produced at least one draw
    std::size_t entry_count    {0}; // entries drawn (after flag filtering and culling)
    std::size_t draw_call_count{0}; // multi-draw submissions (chunks)
    std::size_t culled_count   {0}; // entries passing the filter but outside the view frustum
};

// Resolved shader stages for one color view configuration (R19).
class Draw_list_color_resolution
{
public:
    uint16_t                                        multiview_count{0}; // key value: 0 single view, N >= 2 multiview
    const erhe::graphics::Reloadable_shader_stages* stages         {nullptr};
};

// Resolved shader stages of one draw list (R17): color per view
// configuration (invalidated wholesale by an environment configuration
// change, R18), shadow per sub-variant (never invalidated - the shadow key
// has no environment). Owned by the render phase (Draw_list_scene keeps one
// per list index, render thread only) and filled lazily on the first draw of
// each (view configuration, sub-variant).
class Draw_list_resolutions
{
public:
    // Draw_list::identity of the list these were resolved for; a different
    // identity at the same list index (after a rebuild) clears them.
    uint64_t                                          identity{0};
    std::vector<Draw_list_color_resolution>           color;
    std::array<
        const erhe::graphics::Reloadable_shader_stages*,
        static_cast<std::size_t>(Shadow_sub_variant::count)
    >                                                 shadow{nullptr, nullptr, nullptr};
    // Set when a resolution attempt failed (no variant); avoids re-logging
    // and re-trying every frame.
    bool                                              color_failed {false};
    bool                                              shadow_failed{false};
};

// The world AABBs of a list's entries in SoA form, parallel to
// Draw_list::entries (appended and swap-removed together): the center and
// the half extents, one array per component, so the per-frame
// classification reads each component contiguously over consecutive entries
// (the lane-wise layout a views-outside, entries-inside loop vectorizes
// over, doc/plans/culling.md phase 2). center and extent are computed
// exactly as erhe::math::aabb_in_convex_volume() computes them from the box.
// An entry without bounds (an invalid AABB: skinned objects carry none)
// stores a negative extent_x and passes every view.
class Draw_list_bounds
{
public:
    // Entries appended by a resize have no bounds (negative extent).
    void resize(std::size_t count);
    void set   (std::size_t index, const erhe::math::Aabb& aabb);
    // Swap-remove support: copies entry from over entry to.
    void move  (std::size_t from, std::size_t to);
    // assign() of every array, reusing the capacity this copy already has.
    void assign(const Draw_list_bounds& source);
    // The one definition of the no-bounds sentinel: 1 for an entry whose
    // extent_x marks it as having no bounds, 0 otherwise (a flag, not a bool,
    // so the culling lane loop that uses it vectorizes under MSVC).
    [[nodiscard]] static auto get_unbounded_flag(const float extent_x) -> uint32_t { return (extent_x >= 0.0f) ? 0u : 1u; }

    std::vector<float> center_x;
    std::vector<float> center_y;
    std::vector<float> center_z;
    std::vector<float> extent_x;
    std::vector<float> extent_y;
    std::vector<float> extent_z;
};

// A group of entries that share one Draw_list_key and can therefore be
// drawn with one pipeline, one buffer bind and one multi-draw
// (doc/erhe/draw_list_renderer.md R13/R14). Owned by
// Draw_list_scene; entries are appended on registration and swap-removed on
// unregistration (Draw_list_scene patches the moved entry's owner record).
//
// The same type is the working copy the prepare phase maintains and the
// copy one frame-in-flight slot publishes for the render phase
// (Draw_list_scene::publish, doc/erhe/draw_list_renderer.md section 9.3):
// the render phase reads only a published copy. The resolved shader stages
// are not part of a list; the render phase caches them per list identity.
class Draw_list
{
public:
    Draw_list_key                                     key;
    std::vector<Draw_list_entry>                      entries;
    // Normative CPU copy of the per-primitive GPU record of every entry
    // (doc/erhe/draw_list_performance_improvements.md): entries.size() records of
    // Draw_list_scene::get_primitive_record_stride() bytes each, in the exact
    // Primitive_interface::primitive_struct layout, parallel to entries
    // (swap-removed together). Written at registration and kept current by
    // the transform / refresh / bounds hooks and the joint slot sync;
    // Primitive_buffer::update(Draw_list, ...) memcpys them per pass and only
    // patches the pass-dependent color / size fields.
    std::vector<std::byte>                            primitive_records;
    // The entries' world AABBs (Draw_list_entry::world_aabb) in SoA form,
    // parallel to entries; written by the same registration, transform and
    // bounds hooks and published with the entries. The render phase
    // classifies them against the frame's culling views.
    Draw_list_bounds                                  bounds;
    // Unique per list creation within a Draw_list_scene (a rebuild creates
    // new lists, possibly at the same indices): what the render phase's
    // resolution cache is keyed on.
    uint64_t                                          identity{0};
    // Change serial: on the working copy, the value of the scene's serial
    // counter at the last change of key, entries or records (never 0); on a
    // published copy, the working copy's serial it was copied at.
    uint64_t                                          serial  {0};
};

} // namespace erhe::scene_renderer
