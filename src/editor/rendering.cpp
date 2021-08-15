#include "rendering.hpp"
#include "application.hpp"
#include "log.hpp"
#include "tools.hpp"
#include "view.hpp"
#include "renderers/forward_renderer.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/line_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/shadow_renderer.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "scene/scene_root.hpp"
#include "windows/viewport_config.hpp"
#include "windows/viewport_window.hpp"
#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/xr/headset.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/tracy_client.hpp"

#include <glm/trigonometric.hpp>

namespace editor {

using namespace erhe::graphics;

Editor_rendering::Editor_rendering()
    : erhe::components::Component{c_name}
{
}

Editor_rendering::~Editor_rendering()
{
}

void Editor_rendering::connect()
{
    m_application            = get<Application     >();
    m_editor_view            = get<Editor_view     >();
    m_editor_tools           = get<Editor_tools    >();
    m_forward_renderer       = get<Forward_renderer>();
    m_id_renderer            = get<Id_renderer     >();
    m_line_renderer          = get<Line_renderer   >();
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
    m_scene_manager          = require<Scene_manager>();
    m_scene_root             = require<Scene_root   >();
    m_shadow_renderer        = get<Shadow_renderer>();
    m_text_renderer          = get<Text_renderer  >();
    m_viewport_config        = get<Viewport_config>();
    m_viewport_window        = get<Viewport_window>();

    require<Mesh_memory>();
}

void Editor_rendering::initialize_component()
{
    if constexpr (s_enable_headset)
    {
        m_headset = std::make_unique<erhe::xr::Headset>(m_application->get_context_window());

        auto  mesh_memory = get<Mesh_memory>();
        auto* view_root   = m_scene_manager->get_view_camera()->node().get();

        m_controller_visualization = std::make_unique<Controller_visualization>(*mesh_memory.get(),
                                                                                *m_scene_root.get(),
                                                                                view_root);
    }
}

void Editor_rendering::init_state()
{
    gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
    gl::disable     (gl::Enable_cap::primitive_restart);
    gl::enable      (gl::Enable_cap::primitive_restart_fixed_index);
    gl::enable      (gl::Enable_cap::texture_cube_map_seamless);
}

auto Editor_rendering::width() const -> int
{
    return m_editor_view->width();
}

auto Editor_rendering::height() const -> int
{
    return m_editor_view->height();
}

void Editor_rendering::begin_frame()
{
    ZoneScoped;

    if constexpr (s_enable_gui)
    {
        m_editor_tools->gui_begin_frame();
        const auto size       = m_viewport_window->content_region_size();
        scene_viewport.width  = size.x;
        scene_viewport.height = size.y;
    }
    else
    {
        scene_viewport.width  = width();
        scene_viewport.height = height();
    }

    if (m_controller_visualization && m_headset)
    {
        m_controller_visualization->update(m_headset->controller_pose());
    }
}

auto Editor_rendering::is_primary_scene_output_framebuffer_ready() -> bool
{
    if constexpr (s_enable_gui)
    {
        return m_viewport_window->is_framebuffer_ready();
    }
    else
    {
        return true;
    }
}

void Editor_rendering::gui_render()
{
    ZoneScoped;

    Expects(m_pipeline_state_tracker);

    ImGui::ShowDemoWindow();
    ImGui::Render();

    // Pipeline state required for NVIDIA driver not to complain about texture
    // unit state when doing the clear.
    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(&Color_blend_state::color_blend_disabled);
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    gl::disable         (gl::Enable_cap::framebuffer_srgb);
    gl::viewport        (0, 0, width(), height());
    gl::clear_color     (0.0f, 0.0f, 0.2f, 0.1f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);
    m_editor_tools->imgui_render();
}

auto Editor_rendering::is_content_in_focus() const -> bool
{
    return s_enable_gui ? m_viewport_window->is_focused() : true;
}

auto Editor_rendering::to_scene_content(const glm::vec2 position_in_root) const -> glm::vec2
{
    if constexpr (s_enable_gui)
    {
        return m_viewport_window->to_scene_content(position_in_root);
    }
    else
    {
        return { position_in_root.x, height() - position_in_root.y };
    }
}

void Editor_rendering::render(const double time)
{
    ZoneScoped;

    Expects(m_application);
    Expects(m_scene_manager);
    Expects(m_editor_view);

    auto& pointer_context = m_editor_view->pointer_context;

    const int w = width();
    const int h = height();
    if ((w == 0) || (h == 0))
    {
        return;
    }

    if (m_trigger_capture)
    {
        m_application->begin_renderdoc_capture();
    }

    begin_frame();

    const Render_context render_context{&pointer_context,
                                        m_scene_manager.get(),
                                        m_line_renderer.get(),
                                        m_text_renderer.get(),
                                        scene_viewport,
                                        time};

    m_editor_tools->render_update_tools(render_context);

    auto* camera = m_scene_manager ? m_scene_manager->get_view_camera().get() : nullptr;
    if ((scene_viewport.width  > 0) &&
        (scene_viewport.height > 0) &&
        is_primary_scene_output_framebuffer_ready() &&
        (camera != nullptr))
    {
        if (m_scene_manager)
        {
            m_scene_manager->sort_lights();
            render_shadowmaps();

            if (m_id_renderer && pointer_context.pointer_in_content_area())
            {
                render_id(time);
            }
        }

        bind_primary_scene_output_framebuffer();
        render_clear_primary();

        if (m_scene_manager && m_forward_renderer)
        {
            render_content    (camera, scene_viewport);
            render_selection  (camera, scene_viewport);
            render_tool_meshes(camera, scene_viewport);
        }

        gl::disable(gl::Enable_cap::framebuffer_srgb);

        m_editor_view->update_pointer();

        auto& pointer_context = m_editor_view->pointer_context;
        const Render_context render_context{&pointer_context,
                                            m_scene_manager.get(),
                                            m_line_renderer.get(),
                                            m_text_renderer.get(),
                                            scene_viewport,
                                            time};

        m_editor_tools->update_and_render_tools(render_context);

        if (m_line_renderer)
        {
            m_line_renderer->render(scene_viewport, *camera);
        }

        if (m_text_renderer)
        {
            m_text_renderer->render(scene_viewport);
        }

        end_primary_framebuffer();
    }

    if constexpr (s_enable_gui)
    {
        pointer_context.priority_action = m_editor_tools->get_priority_action();
        m_editor_tools->imgui();
        gui_render();
    }

    if constexpr (s_enable_headset)
    {
        render_headset();
    }

    if (m_shadow_renderer)  m_shadow_renderer ->next_frame();
    if (m_id_renderer)      m_id_renderer     ->next_frame();
    if (m_forward_renderer) m_forward_renderer->next_frame();
    if (m_text_renderer)    m_text_renderer   ->next_frame();
    if (m_line_renderer)    m_line_renderer   ->next_frame();
}

Headset_view_resources::Headset_view_resources(erhe::xr::Render_view& render_view,
                                               Editor_rendering&      rendering)
{
    log_headset.trace("Color tex {:2} {:<20} depth texture {:2} {:<20} size {:4} x {:4}, hfov {: 6.2f}..{: 6.2f}, vfov {: 6.2f}..{: 6.2f}, pos {}\n",
                      render_view.color_texture,
                      gl::c_str(render_view.color_format),
                      render_view.depth_texture,
                      gl::c_str(render_view.depth_format),
                      render_view.width,
                      render_view.height,
                      glm::degrees(render_view.fov_left ),
                      glm::degrees(render_view.fov_right),
                      glm::degrees(render_view.fov_up   ),
                      glm::degrees(render_view.fov_down ),
                      render_view.view_pose.position);

    Texture_create_info color_texture_create_info;
    color_texture_create_info.target            = gl::Texture_target::texture_2d;
    color_texture_create_info.internal_format   = render_view.color_format;
    color_texture_create_info.wrap_texture_name = render_view.color_texture;
    color_texture_create_info.width             = render_view.width;
    color_texture_create_info.height            = render_view.height;
    color_texture_create_info.use_mipmaps       = false;
    color_texture_create_info.level_count       = 1;
    color_texture = std::make_shared<Texture>(color_texture_create_info);

    Texture_create_info depth_texture_create_info;
    depth_texture_create_info.target            = gl::Texture_target::texture_2d;
    depth_texture_create_info.internal_format   = render_view.depth_format;
    depth_texture_create_info.wrap_texture_name = render_view.depth_texture;
    depth_texture_create_info.width             = render_view.width;
    depth_texture_create_info.height            = render_view.height;
    depth_texture_create_info.use_mipmaps       = false;
    depth_texture_create_info.level_count       = 1;
    depth_texture = std::make_shared<Texture>(depth_texture_create_info);

    // depth_stencil_renderbuffer = std::make_unique<Renderbuffer>(gl::Internal_format::depth24_stencil8,
    //                                                             texture_create_info.sample_count,
    //                                                             render_view.width,
    //                                                             render_view.height);

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0,  color_texture.get());
    create_info.attach(gl::Framebuffer_attachment::depth_attachment,   depth_texture.get());
    //create_info.attach(gl::Framebuffer_attachment::stencil_attachment, depth_stencil_renderbuffer.get());
    framebuffer = std::make_unique<Framebuffer>(create_info);
    framebuffer->set_debug_label("Headset view");

    gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0};
    gl::named_framebuffer_draw_buffers(framebuffer->gl_name(), 1, &draw_buffers[0]);
    gl::named_framebuffer_read_buffer (framebuffer->gl_name(), gl::Color_buffer::color_attachment0);

    camera = std::make_shared<erhe::scene::Camera>("Camera");

    auto scene_root = rendering.get<Scene_root>();
    scene_root->scene().cameras.push_back(camera);

    camera_node = std::make_shared<erhe::scene::Node>();
    scene_root->scene().nodes.emplace_back(camera_node);
    camera_node->parent = rendering.get<Scene_manager>()->get_view_camera()->node().get();
    //camera_node->parent = nullptr;
    camera_node->attach(camera);
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
    erhe::primitive::Gl_geometry_uploader uploader{buffer_transfer_queue,
                                                   mesh_memory.vertex_format_info(),
                                                   mesh_memory.vertex_buffer_info()};
    auto controller_pg = make_primitive_shared(controller_geometry, uploader);
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

auto Editor_rendering::get_headset_view_resources(erhe::xr::Render_view& render_view) -> Headset_view_resources&
{
    auto match_color_texture = [&render_view](const auto& i) {
        return i.color_texture->gl_name() == render_view.color_texture;
    };
    auto i = std::find_if(m_view_resources.begin(), m_view_resources.end(), match_color_texture);
    if (i == m_view_resources.end())
    {
        auto& j = m_view_resources.emplace_back(render_view, *this);
        return j;
    }
    return *i;
}

void Editor_rendering::render_headset()
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
            view_resources.update(render_view, * this);
            auto* framebuffer    = view_resources.framebuffer.get();
            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, framebuffer->gl_name());
            auto status = gl::check_named_framebuffer_status(framebuffer->gl_name(), gl::Framebuffer_target::draw_framebuffer);
            if (status != gl::Framebuffer_status::framebuffer_complete)
            {
                log_headset.error("view framebuffer status = {}\n", c_str(status));
            }
            erhe::scene::Viewport viewport;
            viewport.x      = 0;
            viewport.y      = 0;
            viewport.width  = render_view.width;
            viewport.height = render_view.height;
            gl::viewport(0, 0, render_view.width, render_view.height);

            render_clear_primary();

            if (!m_headset->squeeze_click())
            {
                view_resources.camera_node->update();
                auto* camera = view_resources.camera.get();
                render_content(camera, viewport);

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

void Editor_rendering::render_shadowmaps()
{
    auto* camera = m_scene_manager->get_view_camera().get();
    if (camera == nullptr)
    {
        return;
    }

    m_shadow_renderer->render(m_scene_root->content_layers(), *camera);
}

void Editor_rendering::render_id(const double time)
{
    ZoneScoped;

    auto* camera = m_scene_manager->get_view_camera().get();
    if (camera == nullptr)
    {
        return;
    }

    auto& pointer_context = m_editor_view->pointer_context;

    m_id_renderer->render(scene_viewport,
                          m_scene_root->content_layers(),
                          m_scene_root->tool_layers(),
                          *camera,
                          time,
                          pointer_context.pointer_x,
                          pointer_context.pointer_y);
}

void Editor_rendering::render_clear_primary()
{
    ZoneScoped;

    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(&Color_blend_state::color_blend_disabled);
    gl::viewport(scene_viewport.x,
                 scene_viewport.y,
                 scene_viewport.width,
                 scene_viewport.height);
    gl::enable(gl::Enable_cap::framebuffer_srgb);
    gl::clear_color(m_viewport_config->clear_color[0],
                    m_viewport_config->clear_color[1],
                    m_viewport_config->clear_color[2],
                    m_viewport_config->clear_color[3]);
    gl::clear_depth_f(erhe::graphics::Configuration::depth_clear_value);
    gl::clear(gl::Clear_buffer_mask::color_buffer_bit | gl::Clear_buffer_mask::depth_buffer_bit);
}

void Editor_rendering::render_content(erhe::scene::ICamera*       camera,
                                      const erhe::scene::Viewport viewport)
{
    ZoneScoped;

    if (!m_viewport_config)
    {
        return;
    }

    if (camera == nullptr)
    {
        return;
    }

    auto& render_style = m_viewport_config->render_style_not_selected;

    if (render_style.polygon_fill)
    {
        if (render_style.polygon_offset_enable)
        {
            gl::enable(gl::Enable_cap::polygon_offset_fill);
            gl::polygon_offset_clamp(render_style.polygon_offset_factor,
                                     render_style.polygon_offset_units,
                                     render_style.polygon_offset_clamp);
        }
        m_forward_renderer->render(viewport,
                                   *camera,
                                   m_scene_root->content_layers(),
                                   m_scene_root->materials(),
                                   { Forward_renderer::Pass::polygon_fill },
                                   erhe::scene::Mesh::c_visibility_content);
        gl::disable(gl::Enable_cap::polygon_offset_line);
    }
    if (render_style.edge_lines)
    {
        gl::enable(gl::Enable_cap::sample_alpha_to_coverage);
        m_forward_renderer->primitive_color_source   = render_style.edge_lines_color_source;// Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_constant_color = render_style.line_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_size  = render_style.line_width;
        m_forward_renderer->render(viewport,
                                   *camera,
                                   m_scene_root->content_fill_layers(),
                                   m_scene_root->materials(),
                                   { Forward_renderer::Pass::edge_lines },
                                   erhe::scene::Mesh::c_visibility_content);
        gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    }
    if (render_style.polygon_centroids)
    {
        m_forward_renderer->primitive_color_source   = render_style.polygon_centroids_color_source; // Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_constant_color = render_style.centroid_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_size  = render_style.point_size;
        m_forward_renderer->render(viewport,
                                   *camera,
                                   m_scene_root->content_layers(),
                                   m_scene_root->materials(),
                                   { Forward_renderer::Pass::polygon_centroids },
                                   erhe::scene::Mesh::c_visibility_content);
    }
    if (render_style.corner_points)
    {
        m_forward_renderer->primitive_color_source   = render_style.corner_points_color_source; // Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_constant_color = render_style.corner_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_size  = render_style.point_size;
        m_forward_renderer->render(viewport,
                                   *camera,
                                   m_scene_root->content_layers(),
                                   m_scene_root->materials(),
                                   { Forward_renderer::Pass::corner_points },
                                   erhe::scene::Mesh::c_visibility_content);
    }
}

void Editor_rendering::render_selection(erhe::scene::ICamera*       camera,
                                        const erhe::scene::Viewport viewport)
{
    if (camera == nullptr)
    {
        return;
    }

    auto& render_style = m_viewport_config->render_style_selected;;

    if (m_viewport_config && render_style.edge_lines)
    {
        ZoneScopedN("selection edge lines");

        gl::enable(gl::Enable_cap::sample_alpha_to_coverage);
        m_forward_renderer->primitive_color_source   = Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_constant_color = render_style.line_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_size  = render_style.line_width;
        m_forward_renderer->render(viewport,
                                   *camera,
                                   m_scene_root->selection_layers(),
                                   m_scene_root->materials(),
                                   { Forward_renderer::Pass::edge_lines,
                                     Forward_renderer::Pass::hidden_line_with_blend
                                   },
                                   erhe::scene::Mesh::c_visibility_selection);
        gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    }

    gl::enable(gl::Enable_cap::program_point_size);
    if (m_viewport_config && render_style.polygon_centroids)
    {
        ZoneScopedN("selection polygon centroids");
        m_forward_renderer->primitive_color_source   = Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_color = render_style.centroid_color;
        m_forward_renderer->primitive_constant_size  = render_style.point_size;
        m_forward_renderer->render(viewport,
                                   *camera,
                                   m_scene_root->selection_layers(),
                                   m_scene_root->materials(),
                                   { Forward_renderer::Pass::polygon_centroids },
                                   erhe::scene::Mesh::c_visibility_selection);
    }
    if (m_viewport_config && render_style.corner_points)
    {
        ZoneScopedN("selection corner points");
        m_forward_renderer->primitive_color_source   = Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_color = render_style.corner_color;
        m_forward_renderer->primitive_constant_size  = render_style.point_size;
        m_forward_renderer->render(viewport,
                                   *camera,
                                   m_scene_root->selection_layers(),
                                   m_scene_root->materials(),
                                   { Forward_renderer::Pass::corner_points },
                                   erhe::scene::Mesh::c_visibility_selection);
    }
    gl::disable(gl::Enable_cap::program_point_size);
}

void Editor_rendering::render_tool_meshes(erhe::scene::ICamera*       camera,
                                          const erhe::scene::Viewport viewport)
{
    ZoneScoped;

    if (camera == nullptr)
    {
        return;
    }

    m_forward_renderer->render(viewport,
                               *camera,
                               m_scene_root->tool_layers(),
                               m_scene_root->materials(),
                               {
                                   Forward_renderer::Pass::tag_depth_hidden_with_stencil,
                                   Forward_renderer::Pass::tag_depth_visible_with_stencil,
                                   Forward_renderer::Pass::clear_depth,
                                   Forward_renderer::Pass::depth_only,
                                   Forward_renderer::Pass::require_stencil_tag_depth_visible,
                                   Forward_renderer::Pass::require_stencil_tag_depth_hidden_and_blend,
                               },
                               erhe::scene::Mesh::c_visibility_tool);
}

void Editor_rendering::bind_primary_scene_output_framebuffer()
{
    if constexpr (s_enable_gui)
    {
        m_viewport_window->bind_multisample_framebuffer();
    }
    else
    {
        gl::bind_framebuffer(gl::Framebuffer_target::framebuffer, 0);
        gl::viewport(0, 0, width(), height());
    }
}

// When using gui, resolves multisample framebuffer to resolved framebuffer / texture
// so that we can sample from the texture in GUI scene window.
void Editor_rendering::end_primary_framebuffer()
{
    if constexpr (s_enable_gui)
    {
        m_viewport_window->multisample_resolve();
    }
}


}  // namespace editor
