#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

typedef struct __GLsync *GLsync;

namespace erhe {
    class Item_filter;
}
namespace erhe::graphics {
    class Bind_group_layout;
    class Command_buffer;
    class Render_pass;
    class Gpu_timer;
    class Device;
    class Sampler;
    class Texture;
}
namespace erhe::scene {
    class Camera;
    class Mesh;
    class Skin;
}
namespace erhe::scene_renderer {
    class Content_wide_line_renderer;
    class Face_id_base_provider;
    class Joint_buffer;
    class Mesh_memory;
    class Program_interface;
    class Shader_variant_cache;
}

struct Id_renderer_config;

namespace editor {

class Programs;

class Id_renderer final
{
public:
    bool enabled{true};

    class Id_query_result
    {
    public:
        uint32_t                           id             {0};
        float                              depth          {0.0f};
        std::shared_ptr<erhe::scene::Mesh> mesh           {};
        std::size_t                        index_of_gltf_primitive_in_mesh{0};
        std::size_t                        triangle_id    {std::numeric_limits<std::size_t>::max()};
        bool                               valid          {false};
        uint64_t                           frame_number   {0};
    };

    Id_renderer(
        const Id_renderer_config&                   id_renderer_config,
        erhe::graphics::Device&                     graphics_device,
        erhe::scene_renderer::Program_interface&    program_interface,
        erhe::scene_renderer::Mesh_memory&          mesh_memory,
        erhe::scene_renderer::Shader_variant_cache& shader_variant_cache,
        Programs&                                   programs
    );
    ~Id_renderer() noexcept;

    // Public API

    // Which meshes the ID pass actually rasterizes. Skinned-only is the
    // hybrid default: a separate raytrace pass covers static meshes (it
    // does so correctly because the rest-pose BVH equals the displayed
    // surface) and the ID pass only carries the meshes that GPU skinning
    // posed away from rest. `all` is the legacy "ID renderer covers
    // everything" mode used by the force-id config knob.
    enum class Skinning_filter
    {
        skinned_only,
        all
    };

    class Render_parameters
    {
    public:
        erhe::graphics::Command_buffer&    command_buffer;
        const erhe::math::Viewport&        viewport;
        const erhe::scene::Camera&         camera;
        const std::initializer_list<const std::span<const std::shared_ptr<erhe::scene::Mesh>>>& content_mesh_spans;
        const std::initializer_list<const std::span<const std::shared_ptr<erhe::scene::Mesh>>>& tool_mesh_spans;
        const int                          x;
        const int                          y;
        bool                               reverse_depth{true};
        erhe::math::Depth_range            depth_range{erhe::math::Depth_range::zero_to_one};
        erhe::math::Coordinate_conventions conventions;

        // Joint UBO/SSBO that standard.{vert,frag} reads under
        // ERHE_USE_SKINNING; the id pass uses the same shader pair via
        // ERHE_VARIANT_ID_RENDER, so it needs the same joint binding when
        // any of its buckets contain a skinned mesh. Pass the same
        // Joint_buffer that Forward_renderer uses (Forward_renderer::get_joint_buffer())
        // so both updates allocate disjoint ring ranges.
        erhe::scene_renderer::Joint_buffer*                          joint_buffer{nullptr};
        std::span<const std::shared_ptr<erhe::scene::Skin>>          skins{};
        Skinning_filter                                              skinning_filter{Skinning_filter::all};
    };
    void render            (const Render_parameters& parameters);
    void next_frame        ();

    // ID-buffer edge-line method: render the content edge ribbons -- already
    // queued and compute-expanded by the caller into wide_line_renderer in id
    // mode -- into a viewport-sized rgba8 face-ID buffer with depth test, BEFORE
    // the main viewport pass. The polygon-fill shader then samples
    // get_edge_id_texture() and paints an edge line wherever the stored face id
    // matches its own facet id. This framebuffer is separate from the picking
    // one above: its color is sampled in-frame (not read back) and its depth is
    // cleared then discarded.
    void render_content_edge_id(
        erhe::graphics::Command_buffer&                   command_buffer,
        const erhe::math::Viewport&                       viewport,
        erhe::scene_renderer::Content_wide_line_renderer& wide_line_renderer
    );
    [[nodiscard]] auto get_edge_id_texture() const -> erhe::graphics::Texture*;

    // ID-buffer edge-line method, SEED pass. Renders the visible content fill
    // (the meshes registered for the edge method) outputting each fragment's
    // encoded face id (per-primitive base + facet id, the same registry namespace
    // the edge-id pre-pass and the EDGE_LINES_FROM_ID fill use) into a dedicated
    // face-ID buffer with its own depth buffer, so the buffer holds the FRONTMOST
    // visible face id per pixel. render_content_edge_id() then hands this texture
    // to the wide-line draw, which discards any edge fragment whose own face id
    // does not equal the seed there -- rejecting cap / overspray fragments before
    // they can win the edge-id depth test. Runs BEFORE the edge compute + edge-id
    // pass; its color is sampled in-frame (not read back) and its depth is
    // cleared then discarded.
    class Seed_render_parameters
    {
    public:
        erhe::graphics::Command_buffer&                            command_buffer;
        const erhe::math::Viewport&                                viewport;
        const erhe::scene::Camera&                                 camera;
        const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes;
        const erhe::scene_renderer::Face_id_base_provider*         face_id_base_provider{nullptr};
        bool                                                       reverse_depth{true};
        erhe::math::Depth_range                                    depth_range{erhe::math::Depth_range::zero_to_one};
        erhe::math::Coordinate_conventions                         conventions;
        // Joint UBO/SSBO for skinned content (same Joint_buffer that
        // Forward_renderer uses, so both updates allocate disjoint ring ranges).
        // The seed must apply skinning so its visible surface matches the fill.
        erhe::scene_renderer::Joint_buffer*                        joint_buffer{nullptr};
        std::span<const std::shared_ptr<erhe::scene::Skin>>        skins{};
    };
    void render_content_seed(const Seed_render_parameters& parameters);
    [[nodiscard]] auto get_seed_id_texture() const -> erhe::graphics::Texture*;
    [[nodiscard]] auto get_seed_sampler   () const -> const erhe::graphics::Sampler*;

    [[nodiscard]] auto get(int x, int y, uint32_t& out_id, float& out_depth, uint64_t& out_frame_number) -> bool;
    [[nodiscard]] auto get(int x, int y) -> Id_query_result;

    // Side length of the square readback region (and the natural size for a
    // dedicated pick framebuffer). Callers that render the ID pass from a
    // ray-aligned camera size their viewport to this and query the centre
    // texel (get(get_extent()/2, get_extent()/2)).
    [[nodiscard]] static constexpr auto get_extent() -> int { return s_extent; }


private:
    static constexpr int s_transfer_entry_count = 4;
    static constexpr int s_extent               = 256; // Bigger value handle faster mouse speeds

    class Transfer_entry
    {
    public:
        enum class State : unsigned int {
            Unused = 0,
            Waiting_for_read,
            Read_complete
        };

        erhe::graphics::Ring_buffer_range buffer_range;
        std::vector<uint8_t>              data           {};
        glm::mat4                         clip_from_world{1.0f};
        int                               x_offset       {0};
        int                               y_offset       {0};
        uint64_t                          frame_number   {0};
        int                               slot           {0};
        State                             state          {State::Unused};
    };

    [[nodiscard]] auto get_current_transfer_entry() -> Transfer_entry&;

    void update_framebuffer(erhe::math::Viewport viewport);
    void update_edge_id_framebuffer(erhe::math::Viewport viewport);

    bool                                         m_enabled{true};
    erhe::math::Viewport                         m_viewport{0, 0, 0, 0};

    // TODO Do not store these here?
    erhe::graphics::Device&                      m_graphics_device;
    erhe::scene_renderer::Mesh_memory&           m_mesh_memory;
    erhe::scene_renderer::Shader_variant_cache&  m_shader_variant_cache;
    Programs&                                    m_programs;
    // The bind group layout (Vulkan pipeline layout) shared by all
    // standard.{vert,frag} variants. Must be set on the render command
    // encoder before any buffer bind (Forward_renderer does the same).
    const erhe::graphics::Bind_group_layout*     m_bind_group_layout;
    bool                                         m_y_flip;

    erhe::scene_renderer::Camera_buffer          m_camera_buffers;
    erhe::scene_renderer::Draw_indirect_buffer   m_draw_indirect_buffers;
    erhe::scene_renderer::Primitive_buffer       m_primitive_buffers;

    erhe::graphics::Base_render_pipeline         m_pipeline;
    erhe::graphics::Base_render_pipeline         m_selective_depth_clear_pipeline;
    std::unique_ptr<erhe::graphics::Texture>     m_color_texture;
    std::unique_ptr<erhe::graphics::Texture>     m_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass> m_render_pass;
    std::unique_ptr<erhe::graphics::Gpu_timer>   m_gpu_timer;
    // Edge-ID pre-pass render target (ID-buffer edge-line method), separate from
    // the picking framebuffer above: color is sampled by the polygon-fill pass
    // the same frame, depth is cleared then discarded, and nothing is read back.
    std::unique_ptr<erhe::graphics::Texture>     m_edge_id_color_texture;
    std::unique_ptr<erhe::graphics::Texture>     m_edge_id_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass> m_edge_id_render_pass;
    // Seed pre-pass render target (ID-buffer edge-line method): the visible
    // content fill rendered with VARIANT_FACE_ID_SEED into a face-ID color buffer
    // (sampled by the edge-id pass) with its own depth (frontmost face wins, then
    // discarded). The sampler is nearest/clamp -- the edge-id pass texelFetches by
    // integer pixel, so filtering is irrelevant; created once, size-independent.
    std::unique_ptr<erhe::graphics::Texture>     m_seed_color_texture;
    std::unique_ptr<erhe::graphics::Texture>     m_seed_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass> m_seed_render_pass;
    std::unique_ptr<erhe::graphics::Sampler>     m_seed_sampler;
    erhe::graphics::Ring_buffer_client           m_texture_read_buffer;

    std::array<Transfer_entry, s_transfer_entry_count> m_transfer_entries;
    int                                                m_current_transfer_entry_slot{0};

    class Range
    {
    public:
        uint32_t                           offset   {0};
        uint32_t                           length   {0};
        std::shared_ptr<erhe::scene::Mesh> mesh     {nullptr};
        std::size_t                        mesh_primitive_index{0};
    };

    void render_meshes(
        const Render_parameters&                                   parameters,
        erhe::graphics::Render_command_encoder&                    render_encoder,
        erhe::graphics::Base_render_pipeline&                      pipeline,
        const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes
    );

    // Shared bucket-and-draw core for both the picking ID render (render_meshes)
    // and the edge-line seed render (render_content_seed). Buckets the meshes,
    // resolves a shader variant per bucket (force_enable_mask), and draws them
    // into the supplied render pass with the given primitive settings.
    // use_id_ranges records the id_offset->mesh table the picking readback walks;
    // the seed pass passes false (its color is sampled, never read back).
    void render_buckets(
        erhe::graphics::Render_command_encoder&                    render_encoder,
        erhe::graphics::Base_render_pipeline&                      pipeline,
        const erhe::graphics::Render_pass&                         render_pass,
        const erhe::scene_renderer::Primitive_interface_settings&  primitive_settings,
        uint32_t                                                   force_enable_mask,
        bool                                                       use_id_ranges,
        const erhe::Item_filter&                                   filter,
        const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes
    );

    void update_seed_framebuffer(erhe::math::Viewport viewport);

    std::vector<Range> m_ranges;
    // Scissor optimization: restrict rasterization to the s_extent rect
    // around the pointer (the only region the readback blit copies). The
    // viewport is always set to the full size (see render()), so geometry
    // is transformed identically whether or not the scissor is enabled --
    // the scissor only clips fragments outside the pointer rect.
    bool               m_use_scissor{true};
};

}
