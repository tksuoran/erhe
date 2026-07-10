#pragma once

#include "erhe_renderer/view.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/device.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace erhe::graphics {
    class Compute_command_encoder;
    class Render_command_encoder;
    class Render_pass;
    class Shader_stages;
}

namespace erhe::renderer {

class Debug_renderer;

class Debug_renderer_config
{
public:
    erhe::graphics::Primitive_type primitive_type   {0};
    unsigned int                   stencil_reference{0};
    bool                           draw_visible     {true};
    bool                           draw_hidden      {false};
    bool                           thin_lines       {false};
    // X-ray mode: the hidden (occluded) pass blends at full strength, same as
    // the visible pass, instead of the shared dim hidden-pass constant, so the
    // primitives show through geometry undimmed (e.g. skin bones inside a
    // mesh). Per-fragment strength is still the vertex color's alpha.
    bool                           xray             {false};
};

auto operator==(const Debug_renderer_config& lhs, const Debug_renderer_config& rhs) -> bool;

// Selects which precompiled shader-stage variant and draw-time primitive
// topology a bucket renders with. Modeled on
// erhe::scene_renderer::Shader_key / Shader_key_hash: a small value key with
// a stable hash, used to resolve the pipeline / shader variant once instead
// of branching on device capabilities at every draw.
class Debug_renderer_shader_key
{
public:
    // Shader tier that produces the final on-screen primitives:
    //   simple   - direct vertex buffer -> line_simple.{vert,frag}; the draw
    //              topology is primitive_type (line / triangle / point).
    //   geometry - GL_LINES -> debug_line.{vert,geom} wide-line expansion
    //              (used when compute is unavailable; line only).
    //   compute  - SSBO lines -> compute_before_line.comp -> triangle SSBO ->
    //              graphics (wide lines; line only).
    enum class Tier : uint8_t { simple = 0, geometry = 1, compute = 2 };

    erhe::graphics::Primitive_type primitive_type{erhe::graphics::Primitive_type::line};
    Tier                           tier          {Tier::simple};

    [[nodiscard]] auto operator==(const Debug_renderer_shader_key& other) const noexcept -> bool = default;

    // Trivial collision-free pack: both fields are small enums.
    [[nodiscard]] auto get_hash() const noexcept -> std::uint64_t
    {
        return (static_cast<std::uint64_t>(primitive_type) << 8) |
                static_cast<std::uint64_t>(tier);
    }

    // Only the line primitive uses the wide-line compute / geometry tiers;
    // triangles and points always render directly via the simple tier.
    [[nodiscard]] static auto derive(
        bool                         device_use_compute,
        bool                         device_use_geometry_shader,
        const Debug_renderer_config& config
    ) -> Debug_renderer_shader_key;
};

class Debug_renderer_shader_key_hash
{
public:
    [[nodiscard]] auto operator()(const Debug_renderer_shader_key& key) const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(key.get_hash());
    }
};

class Debug_draw_entry
{
public:
    erhe::graphics::Ring_buffer_range input_buffer_range;
    erhe::graphics::Ring_buffer_range draw_buffer_range;
    // Multiview path only: a per-draw view UBO range carrying
    // stride_per_view = 6 * primitive_count. dispatch_compute() writes
    // and binds it; the render encoder re-binds it on the multiview
    // graphics path so the vertex shader can read stride_per_view.
    // Released in release_buffers().
    erhe::graphics::Ring_buffer_range view_buffer_range;
    std::size_t                       primitive_count;
    bool                              compute_dispatched{false};
};

class Debug_draw_view_span
{
public:
    // Single-view: views.size() == 1, cameras[0] only is populated in
    // the UBO and view_count = 1 at runtime. Multiview: views.size() ==
    // view_count, cameras[0..N-1] are populated and view_count = N
    // so the compute shader loops over all eyes and the multiview
    // vertex shader can slab-index by gl_ViewIndex.
    std::vector<View> views;
    std::size_t       begin;
    std::size_t       end;
};

class Debug_renderer_bucket
{
public:
    Debug_renderer_bucket(erhe::graphics::Device& graphics_device, Debug_renderer& debug_renderer, Debug_renderer_config config);

    void clear           ();
    auto match           (const Debug_renderer_config& config) const -> bool;
    void dispatch_compute(erhe::graphics::Compute_command_encoder& command_encoder);
    void render(
        erhe::graphics::Render_command_encoder& render_encoder,
        const erhe::graphics::Render_pass&      render_pass,
        bool                                    draw_hidden,
        bool                                    draw_visible,
        bool                                    multiview = false
    );
    void release_buffers ();
    auto make_draw       (std::size_t vertex_byte_count, std::size_t primitive_count) -> std::span<std::byte>;
    // Single-view start_view: pushes a 1-camera view span. Multiview
    // overload pushes an N-camera span (N must equal view_count
    // from construction).
    void start_view      (const View& view);
    void start_view      (std::span<const View> views);

private:
    [[nodiscard]] auto make_pipeline     (bool visible) -> erhe::graphics::Base_render_pipeline;
    [[nodiscard]] auto update_view_buffer(
        std::span<const View> views,
        std::size_t           primitive_count_for_stride
    ) -> erhe::graphics::Ring_buffer_range;

    // Tier helpers derived from m_shader_key. uses_compute() == false means
    // the bucket takes the direct (vertex-buffer) draw path -- this is always
    // the case for triangle / point primitives, and for lines when the device
    // has no compute (the geometry or simple line tier renders them).
    [[nodiscard]] auto uses_compute () const -> bool { return m_shader_key.tier == Debug_renderer_shader_key::Tier::compute;  }
    [[nodiscard]] auto uses_geometry() const -> bool { return m_shader_key.tier == Debug_renderer_shader_key::Tier::geometry; }

    erhe::graphics::Device&            m_graphics_device;
    Debug_renderer&                    m_debug_renderer;
    Debug_renderer_shader_key          m_shader_key;
    erhe::graphics::Ring_buffer_client m_view_buffer;

    // Compute path: line vertices -> compute shader -> triangle vertices -> render triangles
    std::optional<erhe::graphics::Ring_buffer_client> m_vertex_ssbo_buffer;
    std::optional<erhe::graphics::Ring_buffer_client> m_triangle_vertex_buffer;

    // Direct path: line / triangle / point vertices -> render GL_LINES / GL_TRIANGLES / GL_POINTS directly
    std::optional<erhe::graphics::Ring_buffer_client> m_line_vertex_buffer;

    Debug_renderer_config                m_config;
    erhe::graphics::Base_render_pipeline m_pipeline_visible;
    erhe::graphics::Base_render_pipeline m_pipeline_hidden;
    std::vector<Debug_draw_entry>        m_draws;
    std::vector<Debug_draw_view_span>    m_view_spans;
    bool                                 m_start_new_draw{true};
};

} // namespace erhe::renderer
