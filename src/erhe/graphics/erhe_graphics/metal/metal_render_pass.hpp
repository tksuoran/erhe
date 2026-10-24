#pragma once

#include "erhe_graphics/render_pass.hpp"
#include "erhe_utility/debug_label.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace MTL { class CommandBuffer; }
namespace MTL { class SamplerState; }
namespace MTL { class Texture; }
namespace MTL { class Fence; }
namespace MTL { class RenderCommandEncoder; }

namespace erhe::graphics {

class Bind_group_layout_impl;
class Render_pipeline_state;

class Render_pass_impl final
{
public:
    Render_pass_impl (Device& device, const Render_pass_descriptor& render_pass_descriptor);
    ~Render_pass_impl() noexcept;
    Render_pass_impl (const Render_pass_impl&) = delete;
    void operator=   (const Render_pass_impl&) = delete;
    Render_pass_impl (Render_pass_impl&&)      = delete;
    void operator=   (Render_pass_impl&&)      = delete;

    [[nodiscard]] auto gl_name                    () const -> unsigned int;
    [[nodiscard]] auto gl_multisample_resolve_name() const -> unsigned int;
    [[nodiscard]] auto get_sample_count           () const -> unsigned int;

    auto check_status() const -> bool;

    [[nodiscard]] auto get_render_target_width () const -> int;
    [[nodiscard]] auto get_render_target_height() const -> int;
    [[nodiscard]] auto get_swapchain           () const -> Swapchain*;
    [[nodiscard]] auto get_debug_label         () const -> erhe::utility::Debug_label;

    [[nodiscard]] static auto get_active_mtl_encoder         () -> MTL::RenderCommandEncoder*;
    [[nodiscard]] static auto get_active_color_pixel_format  (unsigned long index) -> unsigned long;
    [[nodiscard]] static auto get_active_depth_pixel_format  () -> unsigned long;
    [[nodiscard]] static auto get_active_stencil_pixel_format() -> unsigned long;
    [[nodiscard]] static auto get_active_sample_count        () -> unsigned long;

    // The active pass's fragment argument buffer table
    // (Bind_group_layout_impl::Fragment_argument): set_sampled_image() of a
    // fragment-stage dedicated sampler stores its texture and sampler in
    // entry index of layout's table (a different layout starts the table
    // over), and every draw first calls flush_fragment_arguments(), which
    // encodes the table into a fresh ring buffer range when it changed and
    // binds that at [[buffer(15)]] of the fragment stage, the textures made
    // resident for it. The range is released at once: the ring keeps it
    // until the frame retired. No-ops without an active pass.
    static void set_fragment_argument   (const Bind_group_layout_impl& layout, std::size_t index, MTL::Texture* texture, MTL::SamplerState* sampler);
    static void flush_fragment_arguments();

private:
    friend class Render_pass;
    void start_render_pass(Command_buffer& command_buffer, Render_pass* render_pass_before, Render_pass* render_pass_after);
    void end_render_pass  (Command_buffer& command_buffer, Render_pass* render_pass_after);

private:
    Device&                                          m_device;
    Swapchain*                                       m_swapchain{nullptr};
    std::array<Render_pass_attachment_descriptor, 4> m_color_attachments;
    Render_pass_attachment_descriptor                m_depth_attachment;
    Render_pass_attachment_descriptor                m_stencil_attachment;
    int                                              m_render_target_width{0};
    int                                              m_render_target_height{0};
    erhe::utility::Debug_label                       m_debug_label;
    // Non-owning borrow: the MTL::CommandBuffer owned by the
    // Command_buffer that start_render_pass was given. Cleared in
    // end_render_pass.
    MTL::CommandBuffer*                              m_command_buffer{nullptr};
    MTL::RenderCommandEncoder*                       m_mtl_encoder{nullptr};
    // Non-owning borrow of the cb's per-frame fence, used to serialize
    // this render encoder against other encoders on the same cb.
    MTL::Fence*                                      m_inter_encoder_fence{nullptr};
    std::array<unsigned long, 4>                     m_color_pixel_formats{};
    unsigned long                                    m_depth_pixel_format{0};
    unsigned long                                    m_stencil_pixel_format{0};
    unsigned long                                    m_sample_count{1};
    // The fragment argument buffer table (set_fragment_argument()); reset at
    // start_render_pass(), capacity kept.
    const Bind_group_layout_impl*                    m_fragment_argument_layout{nullptr};
    std::vector<MTL::Texture*>                       m_fragment_argument_textures;
    std::vector<MTL::SamplerState*>                  m_fragment_argument_samplers;
    bool                                             m_fragment_arguments_dirty{false};

    friend class Device_impl;
};

} // namespace erhe::graphics
