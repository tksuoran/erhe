#pragma once

#include "erhe_graphics/render_pass.hpp"
#include "erhe_utility/debug_label.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace MTL4 { class ArgumentTable; }
namespace MTL4 { class CommandBuffer; }
namespace MTL4 { class CounterHeap; }
namespace MTL4 { class RenderCommandEncoder; }
namespace MTL { class SamplerState; }
namespace MTL { class Texture; }

namespace erhe::graphics {

class Bind_group_layout_impl;
class Command_buffer_impl;
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

    [[nodiscard]] static auto get_active_mtl_encoder         () -> MTL4::RenderCommandEncoder*;
    // The argument table bound to the active encoder (vertex and fragment
    // stages share it: the buffer index layout is the same for both).
    [[nodiscard]] static auto get_active_argument_table      () -> MTL4::ArgumentTable*;
    // The command buffer the active render pass records into.
    [[nodiscard]] static auto get_active_command_buffer_impl () -> Command_buffer_impl*;
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
    // sets that range's address at index 15 of the pass's argument table
    // (the textures are resident through the device's residency set). The
    // range is released at once: the ring keeps it until the frame retired.
    // No-ops without an active pass.
    static void set_fragment_argument   (const Bind_group_layout_impl& layout, std::size_t index, MTL::Texture* texture, MTL::SamplerState* sampler);
    static void flush_fragment_arguments();

private:
    friend class Render_pass;
    void start_render_pass(Command_buffer& command_buffer, Render_pass* render_pass_before, Render_pass* render_pass_after);
    void end_render_pass  (Command_buffer& command_buffer, Render_pass* render_pass_after);

    // The MTL::Stages (metal4_submission.hpp) of the accesses the
    // attachments' usage_before name (the pass begin barrier's producers)
    // and of those their usage_after name (the pass end barrier's
    // consumers; attachments with user_synchronized left out).
    [[nodiscard]] auto get_usage_before_stages() const -> std::uint64_t;
    [[nodiscard]] auto get_usage_after_stages () const -> std::uint64_t;

private:
    Device&                                          m_device;
    Swapchain*                                       m_swapchain{nullptr};
    std::array<Render_pass_attachment_descriptor, 4> m_color_attachments;
    Render_pass_attachment_descriptor                m_depth_attachment;
    Render_pass_attachment_descriptor                m_stencil_attachment;
    int                                              m_render_target_width{0};
    int                                              m_render_target_height{0};
    erhe::utility::Debug_label                       m_debug_label;
    // Non-owning borrows: the MTL4::CommandBuffer of the Command_buffer
    // that start_render_pass was given, and the argument table it handed
    // this pass. Cleared in end_render_pass.
    Command_buffer_impl*                             m_command_buffer_impl{nullptr};
    MTL4::CommandBuffer*                             m_command_buffer{nullptr};
    MTL4::RenderCommandEncoder*                      m_mtl_encoder{nullptr};
    MTL4::ArgumentTable*                             m_argument_table{nullptr};
    // Gpu_timer end sample of this pass (written at end_render_pass).
    MTL4::CounterHeap*                               m_timer_counter_heap{nullptr};
    std::size_t                                      m_timer_end_sample  {0};
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
