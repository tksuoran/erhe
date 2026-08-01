#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace erhe::graphics {
    class Acceleration_structure;
    class Bind_group_layout;
    class Buffer;
    class Command_buffer;
    class Compute_pipeline;
    class Device;
    class Fragment_outputs;
    class Render_pipeline;
    class Sampler;
    class Shader_resource;
    class Shader_stages;
    class Texture;
}
namespace erhe::primitive {
    class Buffer_mesh;
    class Primitive;
}
namespace erhe::scene { class Mesh; }
namespace erhe::scene_renderer { class Mesh_memory; }

namespace editor {

class Scene_root;

// Lightmap baker (doc/lightmap_baking_plan.md).
//
// Phase 2: the per-instance atlas layout (milestone A) and the texel
// G-buffer raster pass (milestone B). Each lightmapped, non-skinned
// content mesh primitive with channel-2 UVs gets a rectangle in a single
// square atlas page, sized by world-space surface area times texel
// density. The G-buffer pass then rasterizes every region's triangles in
// atlas UV space, storing world position (RGBA32F, w = coverage) and
// world normal (RGBA16F) per texel - the input the ray-query gather (plan
// phase 3) consumes.
//
// UI-free by design (plan section 6) - the Lightmap window and MCP tools
// are thin clients.
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

    Lightmap_baker(erhe::graphics::Device& graphics_device, erhe::scene_renderer::Mesh_memory& mesh_memory);
    ~Lightmap_baker() noexcept;

    [[nodiscard]] auto is_supported() const -> bool;

    // Recompute the atlas layout for the lightmapped, non-skinned content
    // meshes of the scene whose primitives carry channel-2 UVs. Page size
    // grows in power-of-two steps until everything packs (up to s_max_page
    // texels). Returns true when at least one region was packed.
    auto update_layout(Scene_root& scene_root, float texels_per_meter) -> bool;

    [[nodiscard]] auto get_layout() const -> const Atlas_layout& { return m_layout; }

    // Rasterize the texel G-buffer for the current layout: one draw per
    // region, positions mapped through channel-2 UVs into the region's
    // atlas rect. Standalone submit (own command buffer + wait idle) -
    // fine for the phase-2 milestone; phase 3 folds this into the
    // interactive per-frame loop. Returns false when there is no layout
    // or the pipeline is unavailable.
    auto bake_gbuffer() -> bool;

    // Debug: write the G-buffer as 8-bit PNGs (<base>_position.png with
    // position mapped into the layout bounds, <base>_normal.png as
    // normal * 0.5 + 0.5; alpha = coverage). Requires bake_gbuffer().
    auto debug_write_gbuffer_pngs(const std::string& base_path) -> bool;

    // Direct lighting gather (plan phase 3, first milestone): per valid
    // G-buffer texel, explicit sampling of every scene light with a
    // ray-query shadow ray against a BLAS/TLAS of ALL non-skinned content
    // meshes (occluders are not limited to lightmapped meshes). Writes
    // irradiance into the lightmap atlas texture. Standalone submit like
    // bake_gbuffer(); requires bake_gbuffer() first.
    auto bake_direct(Scene_root& scene_root) -> bool;

    // Debug: tone-mapped 8-bit PNG of the baked lightmap atlas.
    auto debug_write_lightmap_png(const std::string& path) -> bool;

    [[nodiscard]] auto get_lightmap_texture() const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_lightmap_texture; }

    [[nodiscard]] auto get_position_texture() const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_position_texture; }
    [[nodiscard]] auto get_normal_texture  () const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_normal_texture; }

    static constexpr int s_min_page = 256;
    static constexpr int s_max_page = 4096;
    static constexpr int s_padding  = 4; // texels around each region (mips + bilinear)

private:
    void ensure_gbuffer_targets();

    class Blas_entry
    {
    public:
        std::shared_ptr<erhe::primitive::Primitive>            primitive; // keeps the Buffer_mesh alive
        std::unique_ptr<erhe::graphics::Acceleration_structure> acceleration_structure;
    };
    auto get_or_create_blas(
        erhe::graphics::Command_buffer&                    command_buffer,
        const std::shared_ptr<erhe::primitive::Primitive>& primitive,
        const erhe::primitive::Buffer_mesh&                buffer_mesh
    ) -> erhe::graphics::Acceleration_structure*;

    erhe::graphics::Device&                            m_graphics_device;
    erhe::scene_renderer::Mesh_memory&                 m_mesh_memory;
    Atlas_layout                                       m_layout;

    // G-buffer raster pass objects (created once in the constructor).
    std::unique_ptr<erhe::graphics::Shader_resource>   m_draw_block; // per-draw UBO: world_from_node + uv_scale_offset
    std::size_t                                        m_draw_block_world_offset {0};
    std::size_t                                        m_draw_block_uv_offset    {0};
    std::size_t                                        m_draw_block_size         {0};
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_bind_group_layout;
    std::unique_ptr<erhe::graphics::Fragment_outputs>  m_fragment_outputs;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_shader_stages;
    std::unique_ptr<erhe::graphics::Render_pipeline>   m_pipeline;
    std::shared_ptr<erhe::graphics::Texture>           m_position_texture;
    std::shared_ptr<erhe::graphics::Texture>           m_normal_texture;
    bool                                               m_gbuffer_valid{false};

    // Direct-light gather objects.
    std::unique_ptr<erhe::graphics::Shader_resource>   m_gather_block;
    std::size_t                                        m_gather_light_count_offset   {0};
    std::size_t                                        m_gather_ray_bias_offset      {0};
    std::size_t                                        m_gather_position_type_offset {0};
    std::size_t                                        m_gather_direction_cos_offset {0};
    std::size_t                                        m_gather_radiance_range_offset{0};
    std::size_t                                        m_gather_params_offset        {0};
    std::size_t                                        m_gather_block_size           {0};
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_gather_layout;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_gather_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>  m_gather_pipeline;
    std::unique_ptr<erhe::graphics::Sampler>           m_nearest_sampler;
    std::shared_ptr<erhe::graphics::Texture>           m_lightmap_texture;
    bool                                               m_lightmap_valid{false};

    std::unordered_map<const erhe::primitive::Buffer_mesh*, Blas_entry> m_blas_cache;
    std::unique_ptr<erhe::graphics::Acceleration_structure>             m_tlas;
    uint32_t                                                            m_tlas_capacity{0};
};

} // namespace editor
