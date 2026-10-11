#pragma once

#include "erhe_renderer/view.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/device.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
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
    // line or triangle; other primitive types are rejected when the bucket is
    // created (Debug_renderer_shader_key::derive).
    erhe::graphics::Primitive_type primitive_type   {erhe::graphics::Primitive_type::line};
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
// a stable hash, used to resolve the pipeline / shader variant once at bucket
// construction instead of branching on the config at every draw.
class Debug_renderer_shader_key
{
public:
    // Shader tier that produces the final on-screen primitives:
    //   simple   - direct vertex buffer -> line_simple.{vert,frag}; the draw
    //              topology is primitive_type (line / triangle).
    //   compute  - SSBO lines -> compute_before_line.comp -> triangle SSBO ->
    //              graphics (wide lines; line only).
    enum class Tier : uint8_t { simple = 0, compute = 1 };

    erhe::graphics::Primitive_type primitive_type{erhe::graphics::Primitive_type::line};
    Tier                           tier          {Tier::simple};

    [[nodiscard]] auto operator==(const Debug_renderer_shader_key& other) const noexcept -> bool = default;

    // Trivial collision-free pack: both fields are small enums.
    [[nodiscard]] auto get_hash() const noexcept -> std::uint64_t
    {
        return (static_cast<std::uint64_t>(primitive_type) << 8) |
                static_cast<std::uint64_t>(tier);
    }

    // Only the line primitive uses the wide-line compute tier; triangles
    // and thin lines always render directly via the simple tier. Verifies
    // that the config's primitive type is line or triangle.
    [[nodiscard]] static auto derive(const Debug_renderer_config& config) -> Debug_renderer_shader_key;
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
    // The span's views are the bucket frame's span_views
    // [first_view, first_view + view_count): a flat per-bucket, per-frame
    // store that is cleared (capacity kept) every frame, so starting a view
    // span does not allocate in steady state.
    // Single-view: view_count == 1, cameras[0] only is populated in
    // the UBO and view_count = 1 at runtime. Multiview: view_count ==
    // Debug_renderer view_count, cameras[0..N-1] are populated and
    // view_count = N so the compute shader loops over all eyes and the
    // multiview vertex shader can slab-index by gl_ViewIndex.
    // frame_slot is the Debug_renderer::begin_frame() slot the span was
    // recorded for: render() draws one slot, release_buffers() releases all
    // slots of one device frame.
    std::size_t first_view;
    std::size_t view_count;
    std::size_t begin;
    std::size_t end;
    std::size_t frame_slot;
};

class Debug_renderer_bucket
{
public:
    Debug_renderer_bucket(erhe::graphics::Device& graphics_device, Debug_renderer& debug_renderer, Debug_renderer_config config);

    // The draws are kept per device frame in flight; every call names the
    // device frame (Device::get_frame_index() of the calling role, which
    // Debug_renderer passes) and touches only that frame's draws.
    auto match           (const Debug_renderer_config& config) const -> bool;
    // Expands the frame's recorded draws not expanded yet.
    void dispatch_compute(erhe::graphics::Compute_command_encoder& command_encoder, uint64_t frame_index);
    // Draws the view spans recorded for frame_slot of the frame; the other
    // slots' draws stay for their own render() call.
    void render(
        erhe::graphics::Render_command_encoder& render_encoder,
        const erhe::graphics::Render_pass&      render_pass,
        bool                                    draw_hidden,
        bool                                    draw_visible,
        bool                                    multiview,
        uint64_t                                frame_index,
        std::size_t                             frame_slot
    );
    // Releases every range the frame's draws hold and clears them.
    void release_buffers (uint64_t frame_index);
    // Records into the draw of the view span start_view() opened last in the
    // frame frame_index.
    auto make_draw       (uint64_t frame_index, std::size_t vertex_byte_count, std::size_t primitive_count) -> std::span<std::byte>;
    // Pushes a view span of the views (1 for single view, N = view_count
    // from construction for multiview) for frame_slot of the frame. The
    // first start_view() of a device frame claims the frame's entry,
    // verifying the frame that used it before released it.
    void start_view      (std::span<const View> views, uint64_t frame_index, std::size_t frame_slot);

private:
    enum class Pass_kind : unsigned int { visible, hidden };
    enum class Draw_kind : unsigned int { core, fringe };
    [[nodiscard]] auto make_pipeline     (Pass_kind pass, Draw_kind draw) -> erhe::graphics::Base_render_pipeline;
    // primitive_count is the dispatched draw's line count: it feeds the
    // compute shader's tail guard, and (multiview only) the per-view SSBO
    // slab stride. The direct path has no compute and passes 0.
    [[nodiscard]] auto update_view_buffer(
        std::span<const View> views,
        std::size_t           primitive_count
    ) -> erhe::graphics::Ring_buffer_range;
    // The draws of one device frame in flight.
    class Frame
    {
    public:
        uint64_t                          frame_index   {std::numeric_limits<uint64_t>::max()};
        std::vector<Debug_draw_entry>     draws         {};
        std::vector<Debug_draw_view_span> view_spans    {};
        std::vector<View>                 span_views    {};
        bool                              start_new_draw{true};
    };
    // The frame's entry when frame_index recorded into it, else nullptr
    // (nothing of this bucket was recorded in that frame).
    [[nodiscard]] auto find_frame(uint64_t frame_index) -> Frame*;
    [[nodiscard]] static auto get_span_views(const Frame& frame, const Debug_draw_view_span& view_span) -> std::span<const View>;

    // Tier helper derived from m_shader_key. uses_compute() == false means
    // the bucket takes the direct (vertex-buffer) draw path -- the case for
    // triangle primitives and for thin lines.
    [[nodiscard]] auto uses_compute() const -> bool { return m_shader_key.tier == Debug_renderer_shader_key::Tier::compute; }

    erhe::graphics::Device&            m_graphics_device;
    Debug_renderer&                    m_debug_renderer;
    Debug_renderer_shader_key          m_shader_key;
    erhe::graphics::Ring_buffer_client m_view_buffer;

    // Compute path: line vertices -> compute shader -> triangle vertices -> render triangles
    std::optional<erhe::graphics::Ring_buffer_client> m_vertex_ssbo_buffer;
    std::optional<erhe::graphics::Ring_buffer_client> m_triangle_vertex_buffer;

    // Direct path: line / triangle vertices -> render GL_LINES / GL_TRIANGLES directly
    std::optional<erhe::graphics::Ring_buffer_client> m_line_vertex_buffer;

    Debug_renderer_config                m_config;
    erhe::graphics::Base_render_pipeline m_pipeline_visible;
    erhe::graphics::Base_render_pipeline m_pipeline_hidden;
    // Fringe draws of the compute tier (anti-aliasing on): stencil compare
    // greater, so a fringe fragment never draws over a pixel a core or an
    // earlier fringe of this bucket claimed.
    erhe::graphics::Base_render_pipeline m_pipeline_visible_fringe;
    erhe::graphics::Base_render_pipeline m_pipeline_hidden_fringe;
    // One entry per device frame in flight, indexed by frame index % size;
    // sized at construction, never resized.
    std::vector<Frame>                   m_frames;
};

} // namespace erhe::renderer
