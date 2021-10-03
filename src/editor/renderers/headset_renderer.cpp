#include "renderers/headset_renderer.hpp"
#include "application.hpp"
#include "configuration.hpp"
#include "log.hpp"
#include "rendering.hpp"
#include "window.hpp"
#include "renderers/line_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/scene_manager.hpp"
#include "scene/scene_root.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/xr/headset.hpp"

namespace editor
{

using namespace erhe::graphics;

Headset_view_resources::Headset_view_resources(erhe::xr::Render_view& render_view,
                                               Editor_rendering&      rendering)
{
    //log_headset.trace("Color tex {:2} {:<20} depth texture {:2} {:<20} size {:4} x {:4}, hfov {: 6.2f}..{: 6.2f}, vfov {: 6.2f}..{: 6.2f}, pos {}\n",
    //                  render_view.color_texture,
    //                  gl::c_str(render_view.color_format),
    //                  render_view.depth_texture,
    //                  gl::c_str(render_view.depth_format),
    //                  render_view.width,
    //                  render_view.height,
    //                  glm::degrees(render_view.fov_left ),
    //                  glm::degrees(render_view.fov_right),
    //                  glm::degrees(render_view.fov_up   ),
    //                  glm::degrees(render_view.fov_down ),
    //                  render_view.view_pose.position);

    Texture_create_info color_texture_create_info;
    color_texture_create_info.target            = gl::Texture_target::texture_2d;
    color_texture_create_info.internal_format   = render_view.color_format;
    color_texture_create_info.wrap_texture_name = render_view.color_texture;
    color_texture_create_info.width             = render_view.width;
    color_texture_create_info.height            = render_view.height;
    color_texture_create_info.use_mipmaps       = false;
    color_texture_create_info.level_count       = 1;
    color_texture = std::make_shared<Texture>(color_texture_create_info);
    color_texture->set_debug_label("Headset_view_resources::color_texture");

    Texture_create_info depth_texture_create_info;
    depth_texture_create_info.target            = gl::Texture_target::texture_2d;
    depth_texture_create_info.internal_format   = render_view.depth_format;
    depth_texture_create_info.wrap_texture_name = render_view.depth_texture;
    depth_texture_create_info.width             = render_view.width;
    depth_texture_create_info.height            = render_view.height;
    depth_texture_create_info.use_mipmaps       = false;
    depth_texture_create_info.level_count       = 1;
    depth_texture = std::make_shared<Texture>(depth_texture_create_info);
    depth_texture->set_debug_label("Headset_view_resources::depth_texture");

    // depth_stencil_renderbuffer = std::make_unique<Renderbuffer>(gl::Internal_format::depth24_stencil8,
    //                                                             texture_create_info.sample_count,
    //                                                             render_view.width,
    //                                                             render_view.height);

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0,  color_texture.get());
    create_info.attach(gl::Framebuffer_attachment::depth_attachment,   depth_texture.get());
    //create_info.attach(gl::Framebuffer_attachment::stencil_attachment, depth_stencil_renderbuffer.get());
    framebuffer = std::make_unique<Framebuffer>(create_info);
    framebuffer->set_debug_label("Headset_view_resources");

    if (!framebuffer->check_status())
    {
        log_headset.warn("Invalid framebuffer for headset - disabling headset");
        return;
    }

    gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0};
    gl::named_framebuffer_draw_buffers(framebuffer->gl_name(), 1, &draw_buffers[0]);
    gl::named_framebuffer_read_buffer (framebuffer->gl_name(), gl::Color_buffer::color_attachment0);

    camera = std::make_shared<erhe::scene::Camera>("Camera");

    auto scene_root = rendering.get<Scene_root>();
    scene_root->scene().cameras.push_back(camera);

    camera_node = std::make_shared<erhe::scene::Node>("Camera");
    scene_root->scene().nodes.emplace_back(camera_node);
    camera_node->parent = rendering.get<Scene_manager>()->get_view_camera()->node().get();
    //camera_node->parent = nullptr;
    camera_node->attach(camera);

    is_valid = true;
}

void Headset_view_resources::update(erhe::xr::Render_view& render_view,
                                    Editor_rendering&      rendering)
{
    camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_xr;
    camera->projection()->fov_left        = render_view.fov_left;
    camera->projection()->fov_right       = render_view.fov_right;
    camera->projection()->fov_up          = render_view.fov_up;
    camera->projection()->fov_down        = render_view.fov_down;
    camera->projection()->z_near          = 0.03f;
    camera->projection()->z_far           = 200.0f;

    render_view.near_depth = 0.03f;
    render_view.far_depth  = 200.0f;

    const glm::mat4 orientation = glm::mat4_cast(render_view.view_pose.orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{ 1 }, render_view.view_pose.position);
    const glm::mat4 m           = translation * orientation;
    camera_node->transforms.parent_from_node.set(m);
    camera_node->update();
}

Controller_visualization::Controller_visualization(Mesh_memory&       mesh_memory,
                                                   Scene_root&        scene_root,
                                                   erhe::scene::Node* view_root)
{
    auto controller_material = scene_root.make_material("Controller", glm::vec4(0.1f, 0.1f, 0.2f, 1.0f));
    //constexpr float length = 0.05f;
    //constexpr float radius = 0.02f;
    auto controller_geometry = erhe::geometry::shapes::make_torus(0.05f, 0.0025f, 22, 8);
    controller_geometry.transform(erhe::toolkit::mat4_swap_yz);
    controller_geometry.reverse_polygons();

    erhe::graphics::Buffer_transfer_queue buffer_transfer_queue;
    auto controller_pg = erhe::primitive::make_primitive_shared(controller_geometry, mesh_memory.build_info_set.gl);
    m_controller_mesh = scene_root.make_mesh_node("Controller",
                                                  controller_pg,
                                                  controller_material,
                                                  *scene_root.controller_layer().get(),
                                                  view_root,
                                                  glm::vec3(-9999.9f, -9999.9f, -9999.9f));
}

void Controller_visualization::update(const erhe::xr::Pose& pose)
{
    const glm::mat4 orientation = glm::mat4_cast(pose.orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{ 1 }, pose.position);
    const glm::mat4 m           = translation * orientation;
    auto node = m_controller_mesh->node();
    node->transforms.parent_from_node.set(m);
    node->update();
}

auto Controller_visualization::get_node() const -> erhe::scene::Node*
{
    return m_controller_mesh->node().get();
}

Headset_renderer::Headset_renderer()
    : erhe::components::Component{c_name}
{
}

Headset_renderer::~Headset_renderer() = default;

auto Headset_renderer::get_headset_view_resources(erhe::xr::Render_view& render_view) -> Headset_view_resources&
{
    auto match_color_texture = [&render_view](const auto& i) {
        return i.color_texture->gl_name() == render_view.color_texture;
    };
    auto i = std::find_if(m_view_resources.begin(), m_view_resources.end(), match_color_texture);
    if (i == m_view_resources.end())
    {
        auto& j = m_view_resources.emplace_back(render_view, *m_editor_rendering);
        return j;
    }
    return *i;
}

void Headset_renderer::render()
{
    if (!m_headset)
    {
        return;
    }

    auto frame_timing = m_headset->begin_frame();
    if (frame_timing.should_render)
    {
        if (m_line_renderer)
        {
            uint32_t red   = 0xff0000ffu;
            uint32_t green = 0xff00ff00u;
            uint32_t blue  = 0xffff0000u;
            auto* controller_node      = m_controller_visualization->get_node();
            auto  controller_position  = controller_node->position_in_world();
            auto  controller_direction = controller_node->direction_in_world();
            auto  end_position         = controller_position - m_headset->trigger_value() * 2.0f * controller_direction;
            glm::vec3 origo {0.0f, 0.0f, 0.0f};
            glm::vec3 unit_x{1.0f, 0.0f, 0.0f};
            glm::vec3 unit_y{0.0f, 1.0f, 0.0f};
            glm::vec3 unit_z{0.0f, 0.0f, 1.0f};
            m_line_renderer->set_line_color(red);
            m_line_renderer->add_lines({{origo, unit_x}}, 10.0f);
            m_line_renderer->set_line_color(green);
            m_line_renderer->add_lines({{origo, unit_y}}, 10.0f);
            m_line_renderer->set_line_color(blue);
            m_line_renderer->add_lines({{origo, unit_z}}, 10.0f);
            m_line_renderer->set_line_color(green);
            m_line_renderer->add_lines({{controller_position, end_position }}, 10.0f);
        }

        auto callback = [this](erhe::xr::Render_view& render_view) -> bool {
            auto& view_resources = get_headset_view_resources(render_view);
            if (!view_resources.is_valid)
            {
                return false;
            }
            view_resources.update(render_view, *m_editor_rendering);
            auto* framebuffer    = view_resources.framebuffer.get();
            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, framebuffer->gl_name());
            auto status = gl::check_named_framebuffer_status(framebuffer->gl_name(), gl::Framebuffer_target::draw_framebuffer);
            if (status != gl::Framebuffer_status::framebuffer_complete)
            {
                log_headset.error("view framebuffer status = {}\n", c_str(status));
            }
            erhe::scene::Viewport viewport;
            viewport.x             = 0;
            viewport.y             = 0;
            viewport.width         = render_view.width;
            viewport.height        = render_view.height;
            viewport.reverse_depth = get<Configuration>()->reverse_depth;
            gl::viewport(0, 0, render_view.width, render_view.height);

            m_editor_rendering->render_clear_primary();

            if (!m_headset->squeeze_click())
            {
                view_resources.camera_node->update();
                auto* camera = view_resources.camera.get();
                m_editor_rendering->render_content(camera, viewport);

                if (m_line_renderer && m_headset->trigger_value() > 0.0f)
                {
                    m_line_renderer->render(viewport, *camera);
                }
            }

            return true;
        };
        m_headset->render(callback);
    }
    m_headset->end_frame();
}

void Headset_renderer::connect()
{
    m_application      = get<Application>();
    m_editor_rendering = get<Editor_rendering>();
    m_line_renderer    = get<Line_renderer   >();
    m_scene_manager    = require<Scene_manager>();
    m_scene_root       = require<Scene_root   >();

    require<Window>();
    require<Configuration>();
}

void Headset_renderer::initialize_component()
{
    if (!get<Configuration>()->openxr)
    {
        return;
    }

    m_headset = std::make_unique<erhe::xr::Headset>(get<Window>()->get_context_window());

    auto  mesh_memory = get<Mesh_memory>();
    auto* view_root   = m_scene_manager->get_view_camera()->node().get();

    m_controller_visualization = std::make_unique<Controller_visualization>(*mesh_memory.get(),
                                                                            *m_scene_root.get(),
                                                                            view_root);
}

void Headset_renderer::begin_frame()
{
    if (m_controller_visualization && m_headset)
    {
        m_controller_visualization->update(m_headset->controller_pose());
    }
}

}