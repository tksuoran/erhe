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

namespace erhe::graphics {

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

    friend class Device_impl;
};

} // namespace erhe::graphics
