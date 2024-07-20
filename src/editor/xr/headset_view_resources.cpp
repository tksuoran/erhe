#include "xr/headset_view_resources.hpp"
#include "editor_log.hpp"
#include "xr/headset_view.hpp"

#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"

namespace editor {

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Headset_view_resources::Headset_view_resources(
    erhe::graphics::Instance& graphics_instance,
    erhe::xr::Render_view&    render_view,
    Headset_view&             headset_view,
    const std::size_t         slot
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

    m_color_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .instance          = graphics_instance,
            .target            = gl::Texture_target::texture_2d,
            .internal_format   = render_view.color_format,
            .width             = m_width,
            .height            = m_height,
            .wrap_texture_name = render_view.color_texture
        }
    );
    m_color_texture->set_debug_label(fmt::format("XR color {}", slot));

    m_depth_stencil_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .instance          = graphics_instance,
            .target            = gl::Texture_target::texture_2d,
            .internal_format   = render_view.depth_stencil_format,
            .width             = m_width,
            .height            = m_height,
            .wrap_texture_name = render_view.depth_stencil_texture,
        }
    );
    m_depth_stencil_texture->set_debug_label(fmt::format("XR depth stencil {}", slot));

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_color_texture.get());
    if (gl_helpers::has_depth(render_view.depth_stencil_format)) {
        create_info.attach(gl::Framebuffer_attachment::depth_attachment, m_depth_stencil_texture.get());
    }
    if (gl_helpers::has_stencil(render_view.depth_stencil_format)) {
        create_info.attach(gl::Framebuffer_attachment::stencil_attachment, m_depth_stencil_texture.get());
    }
    m_framebuffer = std::make_shared<Framebuffer>(create_info);
    m_framebuffer->set_debug_label(fmt::format("XR {}", slot));

    if (!m_framebuffer->check_status()) {
        log_headset->warn("Invalid framebuffer for headset - disabling headset");
        return;
    }

    const gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0 };
    gl::named_framebuffer_draw_buffers(m_framebuffer->gl_name(), 1, &draw_buffers[0]);
    gl::named_framebuffer_read_buffer (m_framebuffer->gl_name(), gl::Color_buffer::color_attachment0);

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
    return m_is_valid;
}

auto Headset_view_resources::get_framebuffer() const -> erhe::graphics::Framebuffer*
{
    return m_framebuffer.get();
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
    return m_color_texture.get();
}

auto Headset_view_resources::get_depth_stencil_texture() const -> erhe::graphics::Texture*
{
    return m_depth_stencil_texture.get();
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

} // namespace editor
