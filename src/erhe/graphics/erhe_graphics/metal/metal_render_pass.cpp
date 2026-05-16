#include "erhe_graphics/metal/metal_render_pass.hpp"
#include "erhe_graphics/metal/metal_command_buffer.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_surface.hpp"
#include "erhe_graphics/metal/metal_swapchain.hpp"
#include "erhe_graphics/metal/metal_texture.hpp"
#include "erhe_graphics/metal/metal_helpers.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

namespace erhe::graphics {

Render_pass_impl*              Device_impl::s_active_render_pass{nullptr};

Render_pass_impl::Render_pass_impl(Device& device, const Render_pass_descriptor& render_pass_descriptor)
    : m_device              {device}
    , m_swapchain           {render_pass_descriptor.swapchain}
    , m_color_attachments   {render_pass_descriptor.color_attachments}
    , m_depth_attachment    {render_pass_descriptor.depth_attachment}
    , m_stencil_attachment  {render_pass_descriptor.stencil_attachment}
    , m_render_target_width {render_pass_descriptor.render_target_width}
    , m_render_target_height{render_pass_descriptor.render_target_height}
    , m_debug_label         {render_pass_descriptor.debug_label}
{
}

Render_pass_impl::~Render_pass_impl() noexcept = default;

void Render_pass_impl::on_thread_enter() {}
void Render_pass_impl::on_thread_exit()  {}

auto Render_pass_impl::gl_name() const -> unsigned int { return 0; }
auto Render_pass_impl::gl_multisample_resolve_name() const -> unsigned int { return 0; }
auto Render_pass_impl::get_sample_count() const -> unsigned int
{
    for (const Render_pass_attachment_descriptor& att : m_color_attachments) {
        if (att.is_defined() && (att.texture != nullptr)) {
            const int sample_count = att.texture->get_sample_count();
            if (sample_count > 0) {
                return static_cast<unsigned int>(sample_count);
            }
        }
    }
    if (m_depth_attachment.is_defined() && (m_depth_attachment.texture != nullptr)) {
        const int sample_count = m_depth_attachment.texture->get_sample_count();
        if (sample_count > 0) {
            return static_cast<unsigned int>(sample_count);
        }
    }
    return 1;
}

void Render_pass_impl::create() {}
void Render_pass_impl::reset()  {}
auto Render_pass_impl::check_status() const -> bool { return true; }

auto Render_pass_impl::get_render_target_width() const -> int { return m_render_target_width; }
auto Render_pass_impl::get_render_target_height() const -> int { return m_render_target_height; }
auto Render_pass_impl::get_swapchain() const -> Swapchain* { return m_swapchain; }
auto Render_pass_impl::get_debug_label() const -> erhe::utility::Debug_label { return m_debug_label; }

auto Render_pass_impl::get_active_mtl_encoder() -> MTL::RenderCommandEncoder*
{
    if (Device_impl::s_active_render_pass != nullptr) {
        return Device_impl::s_active_render_pass->m_mtl_encoder;
    }
    return nullptr;
}

auto Render_pass_impl::get_active_color_pixel_format(const unsigned long index) -> unsigned long
{
    if ((Device_impl::s_active_render_pass != nullptr) && (index < Device_impl::s_active_render_pass->m_color_pixel_formats.size())) {
        return Device_impl::s_active_render_pass->m_color_pixel_formats[index];
    }
    return static_cast<unsigned long>(MTL::PixelFormatInvalid);
}

auto Render_pass_impl::get_active_depth_pixel_format() -> unsigned long
{
    if (Device_impl::s_active_render_pass != nullptr) {
        return Device_impl::s_active_render_pass->m_depth_pixel_format;
    }
    return static_cast<unsigned long>(MTL::PixelFormatInvalid);
}



auto Render_pass_impl::get_active_stencil_pixel_format() -> unsigned long
{
    if (Device_impl::s_active_render_pass != nullptr) {
        return Device_impl::s_active_render_pass->m_stencil_pixel_format;
    }
    return static_cast<unsigned long>(MTL::PixelFormatInvalid);
}

auto Render_pass_impl::get_active_sample_count() -> unsigned long
{
    if (Device_impl::s_active_render_pass != nullptr) {
        return Device_impl::s_active_render_pass->m_sample_count;
    }
    return 1;
}

namespace {

auto to_mtl_load_action(const Load_action action) -> MTL::LoadAction
{
    switch (action) {
        case Load_action::Clear:     return MTL::LoadActionClear;
        case Load_action::Load:      return MTL::LoadActionLoad;
        case Load_action::Dont_care: return MTL::LoadActionDontCare;
        default:                     return MTL::LoadActionDontCare;
    }
}

auto to_mtl_store_action(const Store_action action) -> MTL::StoreAction
{
    switch (action) {
        case Store_action::Dont_care:                      return MTL::StoreActionDontCare;
        case Store_action::Store:                           return MTL::StoreActionStore;
        case Store_action::Multisample_resolve:             return MTL::StoreActionMultisampleResolve;
        case Store_action::Store_and_multisample_resolve:   return MTL::StoreActionStoreAndMultisampleResolve;
        default:                                            return MTL::StoreActionStore;
    }
}

void configure_color_attachment(
    MTL::RenderPassDescriptor*                  render_pass_desc,
    NS::UInteger                                index,
    MTL::Texture*                               texture,
    const Render_pass_attachment_descriptor&     desc
)
{
    MTL::RenderPassColorAttachmentDescriptor* color_att = render_pass_desc->colorAttachments()->object(index);
    color_att->setTexture(texture);
    color_att->setLoadAction(to_mtl_load_action(desc.load_action));
    if (desc.load_action == Load_action::Clear) {
        color_att->setClearColor(MTL::ClearColor(
            desc.clear_value[0],
            desc.clear_value[1],
            desc.clear_value[2],
            desc.clear_value[3]
        ));
    }
    color_att->setStoreAction(to_mtl_store_action(desc.store_action));
    color_att->setLevel(static_cast<NS::UInteger>(desc.texture_level));
    color_att->setSlice(static_cast<NS::UInteger>(desc.texture_layer));

    if ((desc.resolve_texture != nullptr) && (texture->sampleCount() > 1)) {
        MTL::Texture* resolve_mtl_texture = desc.resolve_texture->get_impl().get_mtl_texture();
        if (resolve_mtl_texture != nullptr) {
            color_att->setResolveTexture(resolve_mtl_texture);
            color_att->setResolveLevel(static_cast<NS::UInteger>(desc.resolve_level));
            color_att->setResolveSlice(static_cast<NS::UInteger>(desc.resolve_layer));
        }
    }
}

} // anonymous namespace

void Render_pass_impl::start_render_pass(Command_buffer& command_buffer, Render_pass* const render_pass_before, Render_pass* const render_pass_after)
{
    // Metal handles most cross-pass synchronization automatically via
    // its hazard tracking, so these hints are ignored. The per-cb
    // inter-encoder fence (set up in begin()) handles the cases hazard
    // tracking misses (aliased mip-view reads of parent-texture writes).
    static_cast<void>(render_pass_before);
    static_cast<void>(render_pass_after);

    Device_impl::s_active_render_pass = this;

    // Record into the MTL::CommandBuffer that the caller's
    // Command_buffer owns. The cb must already have begin() called on it
    // by the user.
    Command_buffer_impl& cb_impl = command_buffer.get_impl();
    m_command_buffer       = cb_impl.get_mtl_command_buffer();
    m_inter_encoder_fence  = cb_impl.get_inter_encoder_fence();
    ERHE_VERIFY(m_command_buffer != nullptr);

    MTL::RenderPassDescriptor* render_pass_desc = MTL::RenderPassDescriptor::alloc()->init();

    // Reset pixel format and sample count tracking
    m_color_pixel_formats.fill(static_cast<unsigned long>(MTL::PixelFormatInvalid));
    m_depth_pixel_format   = static_cast<unsigned long>(MTL::PixelFormatInvalid);
    m_stencil_pixel_format = static_cast<unsigned long>(MTL::PixelFormatInvalid);
    m_sample_count         = 1;

    if (m_swapchain != nullptr) {
        // Swapchain render pass - use drawable texture
        Swapchain_impl& swapchain_impl = m_swapchain->get_impl();
        CA::MetalDrawable* drawable = swapchain_impl.get_current_drawable();
        if (drawable == nullptr) {
            // No drawable available (e.g. tick called from SDL event
            // before begin_frame). Bail out cleanly; the caller's cb
            // will be committed empty by submit_command_buffers.
            render_pass_desc->release();
            m_command_buffer      = nullptr;
            m_inter_encoder_fence = nullptr;
            return;
        }
        configure_color_attachment(render_pass_desc, 0, drawable->texture(), m_color_attachments[0]);
        m_color_pixel_formats[0] = static_cast<unsigned long>(drawable->texture()->pixelFormat());
        m_sample_count = drawable->texture()->sampleCount();
    } else {
        // Off-screen render pass - use texture attachments
        for (std::size_t i = 0; i < m_color_attachments.size(); ++i) {
            const Render_pass_attachment_descriptor& att = m_color_attachments[i];
            if (!att.is_defined()) {
                continue;
            }
            MTL::Texture* mtl_texture = att.texture->get_impl().get_mtl_texture();
            if (mtl_texture != nullptr) {
                configure_color_attachment(render_pass_desc, static_cast<NS::UInteger>(i), mtl_texture, att);
                m_color_pixel_formats[i] = static_cast<unsigned long>(mtl_texture->pixelFormat());
                if (i == 0) {
                    m_sample_count = mtl_texture->sampleCount();
                }
            }
        }
    }

    // Depth attachment
    if (m_depth_attachment.is_defined() && (m_depth_attachment.texture != nullptr)) {
        MTL::Texture* depth_texture = m_depth_attachment.texture->get_impl().get_mtl_texture();
        if (depth_texture != nullptr) {
            MTL::RenderPassDepthAttachmentDescriptor* depth_att = render_pass_desc->depthAttachment();
            depth_att->setTexture(depth_texture);
            depth_att->setLoadAction(to_mtl_load_action(m_depth_attachment.load_action));
            if (m_depth_attachment.load_action == Load_action::Clear) {
                depth_att->setClearDepth(m_depth_attachment.clear_value[0]);
            }
            depth_att->setStoreAction(to_mtl_store_action(m_depth_attachment.store_action));
            depth_att->setLevel(static_cast<NS::UInteger>(m_depth_attachment.texture_level));
            depth_att->setSlice(static_cast<NS::UInteger>(m_depth_attachment.texture_layer));
            m_depth_pixel_format = static_cast<unsigned long>(depth_texture->pixelFormat());

            // MSAA depth resolve. Honor the user's resolve_texture / resolve_mode
            // when the source is multisampled and the store action requests a
            // multisample resolve.
            const bool depth_resolves =
                (m_depth_attachment.resolve_texture != nullptr) &&
                (depth_texture->sampleCount() > 1) &&
                ((m_depth_attachment.store_action == Store_action::Multisample_resolve) ||
                 (m_depth_attachment.store_action == Store_action::Store_and_multisample_resolve));
            if (depth_resolves) {
                MTL::Texture* resolve = m_depth_attachment.resolve_texture->get_impl().get_mtl_texture();
                if (resolve != nullptr) {
                    depth_att->setResolveTexture(resolve);
                    depth_att->setResolveLevel(static_cast<NS::UInteger>(m_depth_attachment.resolve_level));
                    depth_att->setResolveSlice(static_cast<NS::UInteger>(m_depth_attachment.resolve_layer));
                    depth_att->setDepthResolveFilter(to_mtl_depth_resolve_filter(m_depth_attachment.resolve_mode));
                }
            }
        }
    }

    // Stencil attachment
    if (m_stencil_attachment.is_defined() && (m_stencil_attachment.texture != nullptr)) {
        MTL::Texture* stencil_texture = m_stencil_attachment.texture->get_impl().get_mtl_texture();
        if (stencil_texture != nullptr) {
            MTL::RenderPassStencilAttachmentDescriptor* stencil_att = render_pass_desc->stencilAttachment();
            stencil_att->setTexture(stencil_texture);
            stencil_att->setLoadAction(to_mtl_load_action(m_stencil_attachment.load_action));
            if (m_stencil_attachment.load_action == Load_action::Clear) {
                stencil_att->setClearStencil(static_cast<uint32_t>(m_stencil_attachment.clear_value[0]));
            }
            stencil_att->setStoreAction(to_mtl_store_action(m_stencil_attachment.store_action));
            stencil_att->setLevel(static_cast<NS::UInteger>(m_stencil_attachment.texture_level));
            stencil_att->setSlice(static_cast<NS::UInteger>(m_stencil_attachment.texture_layer));
            m_stencil_pixel_format = static_cast<unsigned long>(stencil_texture->pixelFormat());

            // MSAA stencil resolve. Stencil resolve filter has only sample0 in
            // the cross-API enum mapping; the helper picks the right Metal value.
            const bool stencil_resolves =
                (m_stencil_attachment.resolve_texture != nullptr) &&
                (stencil_texture->sampleCount() > 1) &&
                ((m_stencil_attachment.store_action == Store_action::Multisample_resolve) ||
                 (m_stencil_attachment.store_action == Store_action::Store_and_multisample_resolve));
            if (stencil_resolves) {
                MTL::Texture* resolve = m_stencil_attachment.resolve_texture->get_impl().get_mtl_texture();
                if (resolve != nullptr) {
                    stencil_att->setResolveTexture(resolve);
                    stencil_att->setResolveLevel(static_cast<NS::UInteger>(m_stencil_attachment.resolve_level));
                    stencil_att->setResolveSlice(static_cast<NS::UInteger>(m_stencil_attachment.resolve_layer));
                    stencil_att->setStencilResolveFilter(to_mtl_stencil_resolve_filter(m_stencil_attachment.resolve_mode));
                }
            }
        }
    }

    m_mtl_encoder = m_command_buffer->renderCommandEncoder(render_pass_desc);
    render_pass_desc->release();

    // Serialize against prior encoders on this cb. Metal's automatic
    // hazard tracking does not catch aliased-view reads of parent-
    // texture writes (post-processing's mip-view pattern), so we use
    // the cb's per-frame inter-encoder fence.
    if ((m_mtl_encoder != nullptr) && (m_inter_encoder_fence != nullptr)) {
        m_mtl_encoder->waitForFence(
            m_inter_encoder_fence,
            MTL::RenderStageVertex | MTL::RenderStageFragment
        );
    }

    ERHE_VERIFY(m_mtl_encoder != nullptr);

    // Apply debug labels to command buffer and encoder
    if (!m_debug_label.empty()) {
        NS::String* label = NS::String::alloc()->init(
            m_debug_label.data(),
            NS::UTF8StringEncoding
        );
        m_command_buffer->setLabel(label);
        m_mtl_encoder->setLabel(label);
        label->release();
    }
}

void Render_pass_impl::end_render_pass(Command_buffer& command_buffer, Render_pass* const render_pass_after)
{
    static_cast<void>(command_buffer);
    static_cast<void>(render_pass_after);

    if (m_mtl_encoder != nullptr) {
        // Signal the cb's inter-encoder fence so subsequent encoders on
        // this cb can wait for our render-pass writes.
        if (m_inter_encoder_fence != nullptr) {
            m_mtl_encoder->updateFence(
                m_inter_encoder_fence,
                MTL::RenderStageFragment
            );
        }
        m_mtl_encoder->endEncoding();
        m_mtl_encoder = nullptr;
    }

    // The MTL::CommandBuffer is owned by the user's Command_buffer; we
    // never commit it here. submit_command_buffers handles commit.
    m_command_buffer      = nullptr;
    m_inter_encoder_fence = nullptr;

    Device_impl::s_active_render_pass = nullptr;
}

} // namespace erhe::graphics
