#include "xr/headset_view_resources.hpp"
#include "xr/headset_renderer.hpp"
#include "log.hpp"
#include "rendering.hpp"

#include "scene/scene_root.hpp"

#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/xr/headset.hpp"

namespace editor
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Headset_view_resources::Headset_view_resources(
    erhe::xr::Render_view& render_view,
    Headset_renderer&      headset_renderer,
    Editor_rendering&      rendering,
    const size_t           slot
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

    color_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .target            = gl::Texture_target::texture_2d,
            .internal_format   = render_view.color_format,
            .width             = static_cast<int>(render_view.width),
            .height            = static_cast<int>(render_view.height),
            .wrap_texture_name = render_view.color_texture
        }
    );
    color_texture->set_debug_label(fmt::format("XR color {}", slot));

    depth_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .target            = gl::Texture_target::texture_2d,
            .internal_format   = render_view.depth_format,
            .width             = static_cast<int>(render_view.width),
            .height            = static_cast<int>(render_view.height),
            .wrap_texture_name = render_view.depth_texture,
        }
    );
    depth_texture->set_debug_label(fmt::format("XR depth {}", slot));

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0, color_texture.get());
    create_info.attach(gl::Framebuffer_attachment::depth_attachment,  depth_texture.get());
    framebuffer = std::make_unique<Framebuffer>(create_info);
    framebuffer->set_debug_label(fmt::format("XR {}", slot));

    if (!framebuffer->check_status())
    {
        log_headset.warn("Invalid framebuffer for headset - disabling headset");
        return;
    }

    const gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0 };
    gl::named_framebuffer_draw_buffers(framebuffer->gl_name(), 1, &draw_buffers[0]);
    gl::named_framebuffer_read_buffer (framebuffer->gl_name(), gl::Color_buffer::color_attachment0);

    camera = std::make_shared<erhe::scene::Camera>(
        fmt::format("Headset Camera slot {}", slot)
    );

    const auto scene_root = rendering.get<Scene_root>();
    scene_root->scene().cameras.push_back(camera);
    scene_root->scene().nodes.emplace_back(camera);
    scene_root->scene().nodes_sorted = false;
    headset_renderer.root_camera()->attach(camera);

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
    camera->set_parent_from_node(m);
}

} // namespace editor
