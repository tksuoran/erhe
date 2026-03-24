#include "erhe_graphics/null/null_render_pass.hpp"
#include "erhe_graphics/device.hpp"

namespace erhe::graphics {

std::mutex                     Render_pass_impl::s_mutex;
std::vector<Render_pass_impl*> Render_pass_impl::s_all_framebuffers;
Render_pass_impl*              Render_pass_impl::s_active_render_pass{nullptr};

Render_pass_impl::Render_pass_impl(Device& device, const Render_pass_descriptor& render_pass_descriptor)
    : m_device              {device}
    , m_swapchain           {render_pass_descriptor.swapchain}
    , m_render_target_width {render_pass_descriptor.render_target_width}
    , m_render_target_height{render_pass_descriptor.render_target_height}
    , m_debug_label         {render_pass_descriptor.debug_label}
{
    const std::lock_guard lock{s_mutex};
    s_all_framebuffers.push_back(this);
}

Render_pass_impl::~Render_pass_impl() noexcept
{
    const std::lock_guard lock{s_mutex};
    s_all_framebuffers.erase(
        std::remove(s_all_framebuffers.begin(), s_all_framebuffers.end(), this),
        s_all_framebuffers.end()
    );
}

void Render_pass_impl::on_thread_enter()
{
    // No-op in null backend
}

void Render_pass_impl::on_thread_exit()
{
    // No-op in null backend
}

auto Render_pass_impl::gl_name() const -> unsigned int
{
    return 0;
}

auto Render_pass_impl::gl_multisample_resolve_name() const -> unsigned int
{
    return 0;
}

auto Render_pass_impl::get_sample_count() const -> unsigned int
{
    return 1;
}

void Render_pass_impl::create()
{
    // No-op in null backend
}

void Render_pass_impl::reset()
{
    // No-op in null backend
}

auto Render_pass_impl::check_status() const -> bool
{
    return true;
}

auto Render_pass_impl::get_render_target_width() const -> int
{
    return m_render_target_width;
}

auto Render_pass_impl::get_render_target_height() const -> int
{
    return m_render_target_height;
}

auto Render_pass_impl::get_swapchain() const -> Swapchain*
{
    return m_swapchain;
}

auto Render_pass_impl::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

void Render_pass_impl::start_render_pass()
{
    // No-op in null backend
}

void Render_pass_impl::end_render_pass()
{
    // No-op in null backend
}

} // namespace erhe::graphics
