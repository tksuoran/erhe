#pragma once

#include "erhe_dataformat/dataformat.hpp"

#include "erhe_texgen/shader_code.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace erhe::graphics {
    class Bind_group_layout;
    class Buffer;
    class Command_buffer;
    class Device;
    class Fragment_outputs;
    class Render_pipeline;
    class Sampler;
    class Shader_stages;
    class Texture;
}
namespace editor {

// One sampler2D input to bind for a render: the GLSL binding index / identifier
// (from the composition's Shader_code::get_samplers()) plus the texture to bind
// there (a buffer node's rendered texture). Used by the Phase 5 buffer cut
// point - a graph that samples buffers passes one of these per buffer.
class Texture_sample_binding
{
public:
    int                            binding{0};        // sampler namespace index, matches "tex_<binding>"
    std::string                    name   {};         // GLSL identifier "tex_<binding>"
    const erhe::graphics::Texture* texture{nullptr};  // bound at draw time
};

// Shared render-to-texture helper for the texture graph.
//
// It takes an erhe::texgen-assembled fragment body plus the ordered uniform
// values (from Shader_code::get_uniforms()) and renders a fullscreen pass into
// a caller-owned color Texture, using the exact Vulkan recipe proven by
// src/erhe/graphics/test/test_texgen_render.cpp: a v_uv fullscreen triangle,
// Uniform_declaration_mode::none, and a std140 UBO block named "params" built
// from the uniform list.
//
// Compiled Shader_stages + Render_pipeline are cached keyed on a hash of the
// assembled source string, so a node whose composition text is unchanged never
// recompiles (only its live UBO values are re-uploaded). This one helper backs
// both the per-node preview thumbnails and the output node's bake, so the
// pipeline / bind-group layouts / sampler / fragment-output declarations are
// created once (in the constructor) and reused.
//
// Steady-state (no dirty nodes) performs no work and no allocation: render_into
// is only called for nodes that changed. begin_frame() recycles the per-frame
// UBO buffer bucket (clear-keep-capacity) so uniform uploads reuse capacity.
class Texture_renderer
{
public:
    explicit Texture_renderer(erhe::graphics::Device& device);
    ~Texture_renderer() noexcept;

    Texture_renderer (const Texture_renderer&) = delete;
    void operator=   (const Texture_renderer&) = delete;
    Texture_renderer (Texture_renderer&&)      = delete;
    void operator=   (Texture_renderer&&)      = delete;

    // Recycles the UBO buffer bucket for the current frame-in-flight slot.
    // Call once per frame before any render_into() calls.
    void begin_frame();

    // Renders the assembled fragment body into target (creating / resizing it
    // to size x size when needed), leaving it in shader_read_only layout so
    // ImGui / materials can sample it immediately. Returns true on success;
    // on shader compile failure it returns false and leaves target unchanged
    // (the caller keeps the previous good texture).
    [[nodiscard]] auto render_into(
        erhe::graphics::Command_buffer&                   command_buffer,
        std::shared_ptr<erhe::graphics::Texture>&         target,
        int                                               size,
        const std::string&                                fragment_body,
        const std::vector<erhe::texgen::Uniform>&         uniforms,
        const std::vector<erhe::texgen::Sampler_binding>& sampler_decls = {},
        const std::vector<Texture_sample_binding>&        sampler_bindings = {}
    ) -> bool;

    // Renders the assembled fragment into a size x size texture and reads it
    // back to host memory as tightly packed 8-bit RGBA (color_format()), using
    // a self-contained command buffer that is submitted and waited on before
    // returning. Unlike render_into(), this does NOT record into a frame command
    // buffer: it owns its own submit + wait_idle, so it must be called on the
    // main thread while NOT inside an open render pass (e.g. from the MCP
    // dispatch, which runs mid-frame with the frame cb merely recording). Intended
    // for infrequent diagnostic export (texture_graph_export_png), not the hot
    // path. Returns true on success; false on shader compile failure.
    [[nodiscard]] auto render_and_read_rgba8(
        int                                               size,
        const std::string&                                fragment_body,
        const std::vector<erhe::texgen::Uniform>&         uniforms,
        std::vector<std::uint8_t>&                        out_pixels,
        const std::vector<erhe::texgen::Sampler_binding>& sampler_decls = {},
        const std::vector<Texture_sample_binding>&        sampler_bindings = {}
    ) -> bool;

    // Color format of textures produced by this helper (linear rgba8).
    [[nodiscard]] static auto color_format() -> erhe::dataformat::Format;

private:
    // One compiled composition: shader stages, the format-matched pipeline and
    // the std140 UBO layout. A cache entry whose shader_stages is null marks a
    // known compile failure so the same bad source is not retried every frame.
    class Compiled
    {
    public:
        std::unique_ptr<erhe::graphics::Shader_stages>    shader_stages;
        std::unique_ptr<erhe::graphics::Render_pipeline>  pipeline;
        // Per-composition layout, built only when the composition samples
        // buffers (UBO at binding 0 + one combined_image_sampler per buffer).
        // Null when the composition has no samplers - the shared m_ubo_layout /
        // m_empty_layout is used instead.
        std::unique_ptr<erhe::graphics::Bind_group_layout> sampler_layout;
        std::vector<std::size_t>                          member_offsets; // parallel to the uniform list
        std::vector<int>                                  sampler_binding_points; // parallel to sampler_decls
        std::size_t                                       ubo_bytes{0};
        bool                                              has_ubo     {false};
        bool                                              has_samplers{false};
        bool                                              valid       {false};
    };

    [[nodiscard]] auto get_compiled(
        const std::string&                                fragment_body,
        const std::vector<erhe::texgen::Uniform>&         uniforms,
        const std::vector<erhe::texgen::Sampler_binding>& sampler_decls
    ) -> const Compiled*;
    [[nodiscard]] auto make_target (int size) -> std::shared_ptr<erhe::graphics::Texture>;

    static constexpr std::size_t s_frame_ring = 4; // safely more than frames in flight

    erhe::graphics::Device&                                          m_device;
    std::unique_ptr<erhe::graphics::Bind_group_layout>              m_empty_layout;
    std::unique_ptr<erhe::graphics::Bind_group_layout>              m_ubo_layout;
    std::unique_ptr<erhe::graphics::Fragment_outputs>              m_fragment_outputs;
    std::unique_ptr<erhe::graphics::Sampler>                        m_sampler;
    std::unordered_map<std::size_t, Compiled>                       m_cache;
    std::array<std::vector<std::shared_ptr<erhe::graphics::Buffer>>, s_frame_ring> m_ubo_ring;
    std::size_t                                                     m_current_slot{0};
};

} // namespace editor
