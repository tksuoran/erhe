#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe::scene { class Mesh; }

namespace editor {

class Scene_root;

// Lightmap baker (doc/lightmap_baking_plan.md).
//
// Phase 2 milestone A: the per-instance atlas layout. Each lightmapped,
// non-skinned content mesh primitive with lightmap UVs gets a rectangle in
// a single square atlas page, sized by its world-space surface area times
// the texel density, packed with the vendored skyline bin packer. The
// packed rectangle maps the primitive's normalized channel-2 UVs into
// atlas space as atlas_uv = uv2 * uv_scale_offset.xy + uv_scale_offset.zw.
//
// The layout is CPU state only; the texel G-buffer pass and the gather
// (plan phases 2B/3) consume it. UI-free by design (plan section 6) - the
// Lightmap window is a thin client.
class Lightmap_baker
{
public:
    // One packed mesh primitive. mesh+primitive_index identify the source;
    // the rect is the content region in texels (padding lives outside it).
    class Instance_region
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        std::size_t                        primitive_index{0};
        glm::vec4                          uv_scale_offset{1.0f, 1.0f, 0.0f, 0.0f};
        int                                x{0};
        int                                y{0};
        int                                width{0};
        int                                height{0};
        float                              world_area{0.0f}; // m^2
    };

    class Atlas_layout
    {
    public:
        int                          width {0};
        int                          height{0};
        std::vector<Instance_region> regions;
    };

    // Recompute the atlas layout for the lightmapped, non-skinned content
    // meshes of the scene whose primitives carry channel-2 UVs. Page size
    // grows in power-of-two steps until everything packs (up to s_max_page
    // texels). Returns true when at least one region was packed.
    auto update_layout(Scene_root& scene_root, float texels_per_meter) -> bool;

    [[nodiscard]] auto get_layout() const -> const Atlas_layout& { return m_layout; }

    static constexpr int s_min_page = 256;
    static constexpr int s_max_page = 4096;
    static constexpr int s_padding  = 4; // texels around each region (mips + bilinear)

private:
    Atlas_layout m_layout;
};

} // namespace editor
