#include "erhe_graphics/gl/gl_render_pass.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_texture.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <thread>

namespace erhe::graphics {


Render_pass_impl::Render_pass_impl(Device& device, const Render_pass_descriptor& descriptor)
    : m_device              {device}
    , m_color_attachments   {descriptor.color_attachments}
    , m_depth_attachment    {descriptor.depth_attachment}
    , m_stencil_attachment  {descriptor.stencil_attachment}
    , m_render_target_width {descriptor.render_target_width}
    , m_render_target_height{descriptor.render_target_height}
    , m_debug_label         {descriptor.debug_label}
    , m_debug_group_name    {fmt::format("Render pass: {}", descriptor.debug_label)}
{
}

Render_pass_impl::~Render_pass_impl() noexcept
{
}

void Render_pass_impl::on_thread_enter()
{
}

void Render_pass_impl::on_thread_exit()
{
}

void Render_pass_impl::reset()
{
}

void Render_pass_impl::create()
{
}

auto Render_pass_impl::get_sample_count() const -> unsigned int
{
}

auto Render_pass_impl::check_status() const -> bool
{
}

auto Render_pass_impl::get_render_target_width() const -> int
{
    return m_render_target_width;
}

auto Render_pass_impl::get_render_target_height() const -> int
{
    return m_render_target_height;
}

auto Render_pass_impl::get_debug_label() const -> const std::string&
{
    return m_debug_label;
}

void Render_pass_impl::start_render_pass()
{
}

void Render_pass_impl::end_render_pass()
{
}

} // namespace erhe::graphics
