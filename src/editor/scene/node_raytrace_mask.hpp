#pragma once

#include <cstdint>

namespace editor
{

class Raytrace_node_mask
{
public:
    static constexpr uint32_t none         = 0u;
    static constexpr uint32_t content      = (1u << 0);
    static constexpr uint32_t shadow_cast  = (1u << 1);
    static constexpr uint32_t tool         = (1u << 2);
    static constexpr uint32_t brush        = (1u << 3);
    static constexpr uint32_t rendertarget = (1u << 4);
    static constexpr uint32_t controller   = (1u << 5);
    static constexpr uint32_t grid         = (1u << 6);

    // Mesh-level marker: set on the IInstance of a Mesh whose `skin` is
    // non-null, in lieu of the role bits (content, tool, brush, ...) the
    // mesh would otherwise carry. Reason: raytrace geometry is built once
    // from rest-pose CPU vertices and is never refit when the joint
    // matrices update, so a hit on a skinned IInstance does not correspond
    // to the surface the user sees. Dropping the role bits causes any
    // ray whose mask does not explicitly include `skinned` to skip the
    // instance (raytrace mask semantics are OR-overlap: a ray hits an
    // instance when ray.mask & instance.mask != 0). The ID renderer
    // covers skinned meshes correctly because it rasterizes the posed
    // surface.
    static constexpr uint32_t skinned      = (1u << 7);

    // Editor-generated bone pick proxy (Item_flags::bone_proxy). Its own bit,
    // not `content`, so bone picking is opt-in: only a ray that asks for bones
    // hits them, and in object mode the proxies are invisible to picking even
    // though they sit in the content scene.
    static constexpr uint32_t bone         = (1u << 8);

    // Picking convenience: matches everything that has any role bit but
    // skips skinned-only instances. Use for the any-hit early-out in
    // Scene_view::update_hover_with_raytrace().
    // `bone` is included: bone proxies are static (their BVH geometry is the
    // shared unit bone, posed by the instance transform), so the any-hit
    // early-out must not miss them. Proxies only carry this bit while bone
    // selection mode is active, so object mode is unaffected.
    static constexpr uint32_t pickable_static =
        content | shadow_cast | tool | brush | rendertarget | controller | grid | bone;
};

}
