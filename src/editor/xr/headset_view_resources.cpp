#include "xr/headset_view_resources.hpp"
#include "editor_log.hpp"
#include "xr/headset_view.hpp"

#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"

namespace editor
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Headset_view_resources::Headset_view_resources(
    erhe::graphics::Instance& graphics_instance,
    erhe::xr::Render_view&    render_view,
    Headset_view&             headset_view,
    const std::size_t         slot
)
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

    width  = static_cast<int>(render_view.width);
    height = static_cast<int>(render_view.height);

    color_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .instance          = graphics_instance,
            .target            = gl::Texture_target::texture_2d,
            .internal_format   = render_view.color_format,
            .width             = width,
            .height            = height,
            .wrap_texture_name = render_view.color_texture
        }
    );
    color_texture->set_debug_label(fmt::format("XR color {}", slot));

    depth_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .instance          = graphics_instance,
            .target            = gl::Texture_target::texture_2d,
            .internal_format   = render_view.depth_format,
            .width             = width,
            .height            = height,
            .wrap_texture_name = render_view.depth_texture,
        }
    );
    depth_texture->set_debug_label(fmt::format("XR depth {}", slot));

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0, color_texture.get());
    create_info.attach(gl::Framebuffer_attachment::depth_attachment,  depth_texture.get());
    framebuffer = std::make_shared<Framebuffer>(create_info);
    framebuffer->set_debug_label(fmt::format("XR {}", slot));

    if (!framebuffer->check_status()) {
        log_headset->warn("Invalid framebuffer for headset - disabling headset");
        return;
    }

    const gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0 };
    gl::named_framebuffer_draw_buffers(framebuffer->gl_name(), 1, &draw_buffers[0]);
    gl::named_framebuffer_read_buffer (framebuffer->gl_name(), gl::Color_buffer::color_attachment0);

    camera = std::make_shared<erhe::scene::Camera>(
        fmt::format("Headset Camera slot {}", slot)
    );

    node = std::make_shared<erhe::scene::Node>(
        fmt::format("Headset Camera node slot {}", slot)
    );

    node->attach(camera);
    node->set_parent(headset_view.get_root_node());

    is_valid = true;
}

void Headset_view_resources::update(erhe::xr::Render_view& render_view)
{
    *camera->projection() = erhe::scene::Projection{
        .projection_type = erhe::scene::Projection::Type::perspective_xr,
        .z_near          = 0.03f,
        .z_far           = 200.0f,
        .fov_left        = render_view.fov_left,
        .fov_right       = render_view.fov_right,
        .fov_up          = render_view.fov_up,
        .fov_down        = render_view.fov_down,
    };

    render_view.near_depth = 0.03f;
    render_view.far_depth  = 200.0f;

    const glm::mat4 orientation = glm::mat4_cast(render_view.view_pose.orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{ 1 }, render_view.view_pose.position);
    const glm::mat4 m           = translation * orientation;
    node->set_parent_from_node(m);
}

} // namespace editor
