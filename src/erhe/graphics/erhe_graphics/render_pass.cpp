#include "erhe_graphics/render_pass.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_render_pass.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_METAL)
# include "erhe_graphics/metal/metal_render_pass.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_NONE)
# include "erhe_graphics/null/null_render_pass.hpp"
#endif

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

Render_pass_attachment_descriptor::Render_pass_attachment_descriptor()
    : clear_value{0.0, 0.0, 0.0, 0.0}
{
}

Render_pass_attachment_descriptor::~Render_pass_attachment_descriptor() noexcept = default;

auto Render_pass_attachment_descriptor::is_defined() const -> bool
{
    if (texture != nullptr) {
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
Render_pass_descriptor::~Render_pass_descriptor() noexcept
{
}
namespace {

void validate_attachment_resolve_mode(
    Device&                                  device,
    const Render_pass_descriptor&            descriptor,
    const char*                              attachment_name,
    const Render_pass_attachment_descriptor& attachment,
    uint32_t                                 supported_modes
)
{
    if (!attachment.is_defined() || (attachment.resolve_texture == nullptr)) {
        return;
    }
    if ((attachment.store_action != Store_action::Multisample_resolve) &&
        (attachment.store_action != Store_action::Store_and_multisample_resolve))
    {
        return;
    }
    const uint32_t requested_bit = 1u << static_cast<unsigned int>(attachment.resolve_mode);
    if ((supported_modes & requested_bit) == 0) {
        device.device_message(Message_severity::error,
            fmt::format(
                "Render_pass '{}': {}.resolve_mode={} is not in the device-supported set 0x{:x}",
                descriptor.debug_label.string_view(),
                attachment_name,
                c_str(attachment.resolve_mode),
                supported_modes
            )
        );
    }
}

} // anonymous namespace

Render_pass::Render_pass(Device& device, const Render_pass_descriptor& descriptor)
    : m_device    {device}
    , m_descriptor{descriptor}
    , m_impl      {std::make_unique<Render_pass_impl>(device, descriptor)}
{
    // Validate that all defined attachments have usage_before and usage_after set
    const std::string_view label_sv = descriptor.debug_label.string_view();
    for (std::size_t i = 0; i < descriptor.color_attachments.size(); ++i) {
        const Render_pass_attachment_descriptor& att = descriptor.color_attachments[i];
        if (!att.is_defined()) {
            continue;
        }
        if (att.usage_before == Image_usage_flag_bit_mask::invalid) {
            device.device_message(Message_severity::error,fmt::format("Render_pass '{}': color_attachments[{}].usage_before is invalid", label_sv, i));
        }
        if (att.usage_after == Image_usage_flag_bit_mask::invalid) {
            device.device_message(Message_severity::error,fmt::format("Render_pass '{}': color_attachments[{}].usage_after is invalid", label_sv, i));
        }
    }
    if (descriptor.depth_attachment.is_defined()) {
        if (descriptor.depth_attachment.usage_before == Image_usage_flag_bit_mask::invalid) {
            device.device_message(Message_severity::error,fmt::format("Render_pass '{}': depth_attachment.usage_before is invalid", label_sv));
        }
        if (descriptor.depth_attachment.usage_after == Image_usage_flag_bit_mask::invalid) {
            device.device_message(Message_severity::error,fmt::format("Render_pass '{}': depth_attachment.usage_after is invalid", label_sv));
        }
    }
    if (descriptor.stencil_attachment.is_defined()) {
        if (descriptor.stencil_attachment.usage_before == Image_usage_flag_bit_mask::invalid) {
            device.device_message(Message_severity::error,fmt::format("Render_pass '{}': stencil_attachment.usage_before is invalid", label_sv));
        }
        if (descriptor.stencil_attachment.usage_after == Image_usage_flag_bit_mask::invalid) {
            device.device_message(Message_severity::error,fmt::format("Render_pass '{}': stencil_attachment.usage_after is invalid", label_sv));
        }
    }

    // Validate depth/stencil resolve modes against the device's supported set.
    // Color resolves are always averaged and have no filter selection, so the
    // resolve_mode field on color attachments is silently ignored.
    const Device_info& info = device.get_info();
    validate_attachment_resolve_mode(device, descriptor, "depth_attachment",   descriptor.depth_attachment,   info.supported_depth_resolve_modes);
    validate_attachment_resolve_mode(device, descriptor, "stencil_attachment", descriptor.stencil_attachment, info.supported_stencil_resolve_modes);

    // If the device doesn't allow independent depth/stencil resolve modes, the
    // two modes must agree (or one must be NONE if independent_resolve_none is
    // supported, but the cross-API enum has no NONE -- the absence of a
    // resolve_texture serves the same purpose).
    const bool depth_resolves =
        descriptor.depth_attachment.is_defined() &&
        (descriptor.depth_attachment.resolve_texture != nullptr) &&
        ((descriptor.depth_attachment.store_action == Store_action::Multisample_resolve) ||
         (descriptor.depth_attachment.store_action == Store_action::Store_and_multisample_resolve));
    const bool stencil_resolves =
        descriptor.stencil_attachment.is_defined() &&
        (descriptor.stencil_attachment.resolve_texture != nullptr) &&
        ((descriptor.stencil_attachment.store_action == Store_action::Multisample_resolve) ||
         (descriptor.stencil_attachment.store_action == Store_action::Store_and_multisample_resolve));
    if (depth_resolves && stencil_resolves &&
        (descriptor.depth_attachment.resolve_mode != descriptor.stencil_attachment.resolve_mode) &&
        !info.independent_depth_stencil_resolve)
    {
        device.device_message(Message_severity::error,
            fmt::format(
                "Render_pass '{}': depth_attachment.resolve_mode={} and stencil_attachment.resolve_mode={} differ, "
                "but the device does not support independent depth/stencil resolve modes",
                descriptor.debug_label.string_view(),
                c_str(descriptor.depth_attachment.resolve_mode),
                c_str(descriptor.stencil_attachment.resolve_mode)
            )
        );
    }
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
auto Render_pass::get_swapchain() const -> Swapchain*
{
    return m_impl->get_swapchain();
}
auto Render_pass::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_impl->get_debug_label();
}
auto Render_pass::get_descriptor() const -> const Render_pass_descriptor&
{
    return m_descriptor;
}

auto Render_pass::get_impl() -> Render_pass_impl&
{
    return *m_impl.get();
}
auto Render_pass::get_impl() const -> const Render_pass_impl&
{
    return *m_impl.get();
}

//

void Render_pass::start_render_pass(Command_buffer& command_buffer, Render_pass* const render_pass_before, Render_pass* const render_pass_after)
{
    Render_pass* const active = m_device.get_active_render_pass();
    if (active != nullptr) {
        m_device.device_message(Message_severity::error,
            fmt::format(
                "Render_pass '{}': start_render_pass() called while render pass '{}' is already active -- render passes must not nest",
                m_descriptor.debug_label.string_view(),
                active->get_debug_label().string_view()
            )
        );
    }
    m_device.set_active_render_pass(this);
    m_impl->start_render_pass(command_buffer, render_pass_before, render_pass_after);
}

void Render_pass::end_render_pass(Render_pass* const render_pass_after)
{
    m_impl->end_render_pass(render_pass_after);
    m_device.set_active_render_pass(nullptr);
}


Scoped_render_pass::Scoped_render_pass(
    Render_pass&       render_pass,
    Command_buffer&    command_buffer,
    Render_pass* const render_pass_before,
    Render_pass* const render_pass_after
)
    : m_render_pass      {render_pass}
    , m_render_pass_after{render_pass_after}
{
    m_render_pass.start_render_pass(command_buffer, render_pass_before, render_pass_after);
}

Scoped_render_pass::~Scoped_render_pass()
{
    m_render_pass.end_render_pass(m_render_pass_after);
}

} // namespace erhe::graphics
