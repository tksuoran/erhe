#include "xr/headset_view_resources.hpp"
#include "editor_log.hpp"
#include "xr/headset_view.hpp"

#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

using erhe::graphics::Render_pass;
using erhe::graphics::Texture;

Headset_view_resources::Headset_view_resources(
    erhe::graphics::Device& graphics_device,
    erhe::xr::Render_view&  render_view,
    Headset_view&           headset_view,
    const std::size_t       slot
)
    : m_headset_view{headset_view}
{
    // log_headset.trace(
    //     "Color tex {:2} {:<20} depth texture {:2} {:<20} size {:4} x {:4}, hfov {: 6.2f}..{: 6.2f}, vfov {: 6.2f}..{: 6.2f}, pos {}\n",
    //     render_view.color_texture,
    //     gl::c_str(render_view.color_format),
    //     render_view.depth_texture,
    //     gl::c_str(render_view.depth_format),
    //     render_view.width,
    //     render_view.height,
    //     glm::degrees(render_view.fov_left ),
    //     glm::degrees(render_view.fov_right),
    //     glm::degrees(render_view.fov_up   ),
    //     glm::degrees(render_view.fov_down ),
    //     render_view.view_pose.position
    // );

    m_width  = static_cast<int>(render_view.width);
    m_height = static_cast<int>(render_view.height);

    m_color_texture         = render_view.color_texture;
    m_depth_stencil_texture = render_view.depth_stencil_texture;

    // The multiview render path leaves per-view textures null because
    // the OpenXR shared swapchain feeds Render_views_frame::shared_*_texture
    // instead, and Headset_view builds its own multiview Render_pass over
    // those layered targets. In that case skip the per-view render-pass
    // construction below; only the Camera + Node members are useful to
    // the multiview path. The per-eye fallback path always has a non-null
    // color texture and continues to build its per-view Render_pass.
    const bool has_per_view_color_texture = (m_color_texture != nullptr);

    if (has_per_view_color_texture) {
        const erhe::dataformat::Format color_format = m_color_texture->get_pixelformat();
        if (color_format != render_view.color_format) {
            log_xr->warn("swapchain color format = {}, expected format = {}", erhe::dataformat::c_str(color_format), erhe::dataformat::c_str(render_view.color_format));
            render_view.color_format = color_format;
        }
        if (m_depth_stencil_texture != nullptr) {
            const erhe::dataformat::Format depth_stencil_format = m_depth_stencil_texture->get_pixelformat();
            if (depth_stencil_format != render_view.depth_stencil_format) {
                log_xr->warn("swapchain depth format = {}, expected format = {}", erhe::dataformat::c_str(depth_stencil_format), erhe::dataformat::c_str(render_view.depth_stencil_format));
                render_view.depth_stencil_format = depth_stencil_format;
            }
        }
    }

    erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
    render_pass_descriptor.color_attachments[0].texture      = m_color_texture;
    render_pass_descriptor.color_attachments[0].load_action  = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.color_attachments[0].store_action = erhe::graphics::Store_action::Store;
    // OpenXR Vulkan2 runtimes hand the color image to the application in
    // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL and require it to be in
    // that same layout at xrReleaseSwapchainImage. Leaving it in
    // shader_read_only_optimal forces the runtime into an unexpected
    // layout transition and shows up as visible head-motion judder /
    // jitter on the WMR / Microsoft OpenXR runtime even when our
    // submission ordering is correct.
    render_pass_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
    render_pass_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::color_attachment_optimal;
    render_pass_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
    render_pass_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::color_attachment_optimal;
    if ((m_depth_stencil_texture != nullptr) && (erhe::dataformat::get_depth_size_bits(render_view.depth_stencil_format) > 0)) {
        render_pass_descriptor.depth_attachment.texture      = m_depth_stencil_texture;
        render_pass_descriptor.depth_attachment.load_action  = erhe::graphics::Load_action::Clear;
        // Clear to the convention's far value (reverse-Z 0.0, forward-Z 1.0); the
        // default 0.0 is the near plane under forward-Z and rejects all geometry.
        render_pass_descriptor.depth_attachment.clear_value[0] = graphics_device.get_reverse_depth() ? 0.0 : 1.0;
        render_pass_descriptor.depth_attachment.store_action = erhe::graphics::Store_action::Store;
        render_pass_descriptor.depth_attachment.usage_before  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        render_pass_descriptor.depth_attachment.layout_before = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
        render_pass_descriptor.depth_attachment.usage_after   = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        render_pass_descriptor.depth_attachment.layout_after  = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    }
    if ((m_depth_stencil_texture != nullptr) && (erhe::dataformat::get_stencil_size_bits(render_view.depth_stencil_format) > 0)) {
        render_pass_descriptor.stencil_attachment.texture       = m_depth_stencil_texture;
        render_pass_descriptor.stencil_attachment.load_action   = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.stencil_attachment.store_action  = erhe::graphics::Store_action::Dont_care;
        render_pass_descriptor.stencil_attachment.usage_before  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        render_pass_descriptor.stencil_attachment.layout_before = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
        render_pass_descriptor.stencil_attachment.usage_after   = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
        render_pass_descriptor.stencil_attachment.layout_after  = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    }
    render_pass_descriptor.render_target_width  = m_width;
    render_pass_descriptor.render_target_height = m_height;
    render_pass_descriptor.debug_label = erhe::utility::Debug_label{fmt::format("XR {}", slot)};
    if (has_per_view_color_texture) {
        m_render_pass = std::make_shared<Render_pass>(graphics_device, render_pass_descriptor);
    }

    m_camera = std::make_shared<erhe::scene::Camera>(
        fmt::format("Headset Camera slot {}", slot)
    );

    m_node = std::make_shared<erhe::scene::Node>(
        fmt::format("Headset Camera node slot {}", slot)
    );

    m_node->attach(m_camera);
    m_node->set_parent(headset_view.get_root_node());

    m_is_valid = true;
}

auto Headset_view_resources::is_valid() const -> bool
{
    // m_is_valid flips true at the end of the ctor, after the Camera +
    // Node members are constructed. Multiview leaves m_color_texture and
    // m_render_pass null on purpose -- the shared OpenXR swapchain feeds
    // Render_views_frame::shared_*_texture, and the multiview render
    // pass is built per-frame in Headset_view::render_headset(). The
    // multiview callback in Headset_view only reads Camera + Node from
    // this object, so m_is_valid alone is the load-bearing post-condition
    // for both paths; gating on the per-view texture / render pass
    // permanently rejects every multiview frame.
    return m_is_valid;
}

auto Headset_view_resources::get_render_pass() const -> erhe::graphics::Render_pass*
{
    return m_render_pass.get();
}

auto Headset_view_resources::get_camera() const -> erhe::scene::Camera*
{
    return m_camera.get();
}

auto Headset_view_resources::get_width() const -> int
{
    return m_width;
}

auto Headset_view_resources::get_height() const -> int
{
    return m_height;
}

auto Headset_view_resources::get_color_texture() const -> erhe::graphics::Texture*
{
    return m_color_texture;
}

auto Headset_view_resources::get_depth_stencil_texture() const -> erhe::graphics::Texture*
{
    return m_depth_stencil_texture;
}

void Headset_view_resources::update(erhe::xr::Render_view& render_view, erhe::scene::Projection::Fov_sides fov_sides)
{
    *m_camera->projection() = erhe::scene::Projection{
        .projection_type = erhe::scene::Projection::Type::perspective_xr,
        .z_near          = 0.03f,
        .z_far           = 200.0f,
        .fov_left        = fov_sides.left,  // render_view.fov_left,
        .fov_right       = fov_sides.right, // render_view.fov_right,
        .fov_up          = fov_sides.up,    // render_view.fov_up,
        .fov_down        = fov_sides.down   // render_view.fov_down,
    };

    render_view.near_depth = 0.03f;
    render_view.far_depth  = 200.0f;

    const glm::vec3 offset = m_headset_view.get_camera_offset();

    const glm::mat4 orientation = glm::mat4_cast(render_view.view_pose.orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{ 1 }, render_view.view_pose.position + offset);
    const glm::mat4 m           = translation * orientation;
    m_node->set_parent_from_node(m);
}

}
