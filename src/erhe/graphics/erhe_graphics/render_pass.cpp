#include "erhe_graphics/render_pass.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_render_pass.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#endif

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"

namespace erhe::graphics {

Render_pass_attachment_descriptor::Render_pass_attachment_descriptor()
    : clear_value{0.0, 0.0, 0.0, 0.0}
{
}

Render_pass_attachment_descriptor::~Render_pass_attachment_descriptor() = default;

auto Render_pass_attachment_descriptor::is_defined() const -> bool
{
    if (texture != nullptr) {
        return true;
    }
    if (use_default_framebuffer) {
        return true;
    }
    return false;
}

auto Render_pass_attachment_descriptor::get_pixelformat() const -> erhe::dataformat::Format
{
    if (texture != nullptr) {
        return texture->get_pixelformat();
    }
    return {};
}

Render_pass_descriptor::Render_pass_descriptor()
{
}
Render_pass_descriptor::~Render_pass_descriptor()
{
}
Render_pass::Render_pass(Device& device, const Render_pass_descriptor& descriptor)
    : m_impl{std::make_unique<Render_pass_impl>(device, descriptor)}
{
}
Render_pass::~Render_pass() noexcept
{
}
auto Render_pass::get_sample_count() const -> unsigned int
{
    return m_impl->get_sample_count();
}
auto Render_pass::get_render_target_width() const -> int
{
    return m_impl->get_render_target_width();
}
auto Render_pass::get_render_target_height() const -> int
{
    return m_impl->get_render_target_height();
}
auto Render_pass::get_debug_label() const -> const std::string&
{
    return m_impl->get_debug_label();
}
auto Render_pass::get_impl() -> Render_pass_impl&
{
    return *m_impl.get();
}
auto Render_pass::get_impl() const -> const Render_pass_impl&
{
    return *m_impl.get();
}

} // namespace erhe::graphics
