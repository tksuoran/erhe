#include "editor.hpp"

#include "application.hpp"
#include "log.hpp"
#include "operations/operation_stack.hpp"

#include "renderers/forward_renderer.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/shadow_renderer.hpp"
#include "renderers/text_renderer.hpp"
#include "renderers/line_renderer.hpp"

#include "scene/scene_manager.hpp"

#include "tools/fly_camera_tool.hpp"
#include "tools/grid_tool.hpp"
#include "tools/hover_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/trs_tool.hpp"

#include "windows/brushes.hpp"
#include "windows/camera_properties.hpp"
#include "windows/light_properties.hpp"
#include "windows/material_properties.hpp"
#include "windows/mesh_properties.hpp"
#include "windows/node_properties.hpp"
#include "windows/operations.hpp"
#include "windows/viewport_config.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics_experimental/shader_monitor.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/window.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/gl/gl.hpp"

#include "erhe/imgui/imgui_impl_erhe.hpp"
#include "backends/imgui_impl_glfw.h"

#include "erhe_tracy.hpp"

#include <GLFW/glfw3.h>

#include <chrono>

namespace editor {

using namespace std;
using namespace erhe::geometry;
using namespace erhe::graphics;
using namespace erhe::toolkit;

Editor::Editor()
    : erhe::components::Component{c_name}
{
}

void Editor::connect()
{
    m_application            = get<Application         >();
    m_forward_renderer       = get<Forward_renderer    >();
    m_id_renderer            = get<Id_renderer         >();
    m_scene_manager          = require<Scene_manager   >();
    m_shader_monitor         = get<Shader_monitor      >();
    m_shadow_renderer        = get<Shadow_renderer     >();
    m_text_renderer          = get<Text_renderer       >();
    m_line_renderer          = get<Line_renderer       >();
    m_pipeline_state_tracker = get<OpenGL_state_tracker>();

    m_selection_tool         = require<Selection_tool  >();
    m_operation_stack        = get<Operation_stack     >();
    m_brushes                = get<Brushes             >();
    m_camera_properties      = get<Camera_properties   >();
    m_hover_tool             = get<Hover_tool          >();
    m_fly_camera_tool        = get<Fly_camera_tool     >();
    m_light_properties       = get<Light_properties    >();
    m_material_properties    = get<Material_properties >();
    m_mesh_properties        = get<Mesh_properties     >();
    m_node_properties        = get<Node_properties     >();
    m_trs_tool               = get<Trs_tool            >();
    m_viewport_window        = get<Viewport_window     >();
    m_operations             = get<Operations          >();
    m_grid_tool              = get<Grid_tool           >();
    m_viewport_config        = get<Viewport_config     >();
}

void Editor::disconnect()
{
    m_application           .reset();
    m_forward_renderer      .reset();
    m_scene_manager         .reset();
    m_shader_monitor        .reset();
    m_shadow_renderer       .reset();
    m_text_renderer         .reset();
    m_line_renderer         .reset();
    m_pipeline_state_tracker.reset();
}

void Editor::initialize_component()
{
    ZoneScoped;

    register_background_tool(m_hover_tool.get());
    register_tool  (m_trs_tool.get());
    register_tool  (m_selection_tool.get());
    register_tool  (m_fly_camera_tool.get());
    register_window(m_camera_properties.get());
    register_window(m_light_properties.get());
    register_window(m_material_properties.get());
    register_tool  (m_mesh_properties.get());
    register_window(m_node_properties.get());
    register_window(m_viewport_window.get());
    register_window(m_operations.get());
    register_tool  (m_brushes.get());

    if (m_selection_tool)
    {
        auto lambda = [this](const Selection_tool::Mesh_collection& meshes)
        {
            auto& layer_meshes = m_scene_manager->selection_layer()->meshes;
            layer_meshes.clear();
            for (auto mesh : meshes)
            {
                layer_meshes.push_back(mesh);
            }
        };
        m_selection_layer_update_subscription = m_selection_tool->subscribe_mesh_selection_change_notification(lambda);
    }

    register_background_tool(m_grid_tool.get());
    register_window(m_viewport_config.get());

    IMGUI_CHECKVERSION();
    m_imgui_context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    //io.ConfigResizeWindowsFromEdges = true;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.Fonts->AddFontFromFileTTF("res/fonts/ProximaNova-Regular.otf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 18.0f);
    //io.Fonts->AddFontDefault();

    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    ImGui::StyleColorsDark();
    auto* glfw_window = reinterpret_cast<GLFWwindow*>(m_application->get_context_window()->get_glfw_window());
    ImGui_ImplGlfw_InitForOther(glfw_window, true);
    ImGui_ImplErhe_Init(m_pipeline_state_tracker);
}

static constexpr const char* c_swap_buffers = "swap buffers";
void Editor::update()
{
    auto new_time = chrono::steady_clock::now();
    chrono::steady_clock::duration duration = new_time - m_current_time;
    double frame_time = chrono::duration<double, ratio<1> >(duration).count();

    if (frame_time > 0.25) {
        frame_time = 0.25;
    }

    m_current_time = new_time;
    m_time_accumulator += frame_time;
    const double dt = 1.0 / 120.0;
    while (m_time_accumulator >= dt)
    {
        update_fixed_step();
        m_time_accumulator -= dt;
        m_time += dt;
    }

    update_once_per_frame();

    if (m_fly_camera_tool)
    {
        m_fly_camera_tool->update_once_per_frame();
    }

    render();

    {
        ZoneScopedN(c_swap_buffers);
        m_application->get_context_window()->swap_buffers();
    }

    if (m_trigger_capture)
    {
        m_application->end_renderdoc_capture();
        m_trigger_capture = false;
    }

    FrameMark;
    TracyGpuCollect
}

void Editor::update_fixed_step()
{
    ZoneScoped;

    if (m_fly_camera_tool)
    {
        m_fly_camera_tool->update_fixed_step();
    }
}

static constexpr const char* c_shader_monitor_poll = "shader monitor poll";
void Editor::update_once_per_frame()
{
    ZoneScoped;

    if (m_shader_monitor)
    {
        ZoneScopedN(c_shader_monitor_poll);
        m_shader_monitor->update_once_per_frame();
    }

    if (m_scene_manager)
    {
        m_scene_manager->update_once_per_frame(m_time);
    }
}

void Editor::gui_begin_frame()
{
    ZoneScoped;

    ImGui_ImplErhe_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Editor::begin_frame()
{
    ZoneScoped;

    if (m_enable_gui)
    {
        gui_begin_frame();
        auto size = m_viewport_window->content_region_size();
        m_scene_viewport.width  = size.x;
        m_scene_viewport.height = size.y;
    }
    else
    {
        m_scene_viewport.width  = width();
        m_scene_viewport.height = height();
    }
}

auto Editor::is_primary_scene_output_framebuffer_ready() -> bool
{
    if (m_enable_gui)
    {
        return m_viewport_window->is_framebuffer_ready();
    }
    else
    {
        return true;
    }
}

void Editor::gui_render()
{
    ZoneScoped;

    ImGui::ShowDemoWindow();
    ImGui::Render();

    // Pipeline state required for NVIDIA driver not to complain about texture unit state
    // when doing the clear.
    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(&Color_blend_state::color_blend_disabled);
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    gl::disable         (gl::Enable_cap::framebuffer_srgb);
    gl::viewport        (0, 0, width(), height());
    gl::clear_color     (0.0f, 0.0f, 0.2f, 0.1f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);
    ImGui_ImplErhe_RenderDrawData(ImGui::GetDrawData());
}

void Editor::render_shadowmaps()
{
    m_shadow_renderer->render(m_scene_manager->content_layers(),
                              m_scene_manager->camera());
}

void Editor::render_id()
{
    ZoneScoped;

    m_id_renderer->render(m_scene_viewport,
                          m_scene_manager->content_layers(),
                          m_scene_manager->tool_layers(),
                          m_scene_manager->camera(),
                          m_time,
                          m_pointer_context.pointer_x,
                          m_pointer_context.pointer_y);
}

void Editor::render_clear_primary()
{
    ZoneScoped;

    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(&Color_blend_state::color_blend_disabled);
    gl::viewport     (m_scene_viewport.x, m_scene_viewport.y, m_scene_viewport.width, m_scene_viewport.height);
    gl::enable       (gl::Enable_cap::framebuffer_srgb);
    gl::clear_color  (m_viewport_config->clear_color[0],
                      m_viewport_config->clear_color[1],
                      m_viewport_config->clear_color[2],
                      m_viewport_config->clear_color[3]);
    gl::clear_depth_f(erhe::graphics::Configuration::depth_clear_value);
    gl::clear        (gl::Clear_buffer_mask::color_buffer_bit | gl::Clear_buffer_mask::depth_buffer_bit);
}

void Editor::render_content()
{
    ZoneScoped;

    if (m_viewport_config && m_viewport_config->polygon_offset_enable)
    {
        gl::enable(gl::Enable_cap::polygon_offset_fill);
        gl::polygon_offset_clamp(m_viewport_config->polygon_offset_factor,
                                 m_viewport_config->polygon_offset_units,
                                 m_viewport_config->polygon_offset_clamp);
    }
    m_forward_renderer->render(m_scene_viewport,
                               m_scene_manager->camera(),
                               m_scene_manager->content_layers(),
                               m_scene_manager->materials(),
                               { Forward_renderer::Pass::polygon_fill },
                               erhe::scene::Mesh::c_visibility_content);
    gl::disable(gl::Enable_cap::polygon_offset_line);
}

void Editor::render_selection()
{
    if (m_viewport_config && m_viewport_config->edge_lines)
    {
        ZoneScopedN("selection edge lines");

        gl::enable(gl::Enable_cap::sample_alpha_to_coverage);
        //gl::enable(gl::Enable_cap::sample_alpha_to_one);
        //m_forward_renderer->primitive_color_source   = Base_renderer::Primitive_color_source::mesh_wireframe_color;
        m_forward_renderer->primitive_color_source   = Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_constant_color = m_viewport_config->line_color;
        //m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::mesh_line_width;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_size  = m_viewport_config->line_width;
        m_forward_renderer->render(m_scene_viewport,
                                   m_scene_manager->camera(),
                                   m_scene_manager->selection_layers(),
                                   m_scene_manager->materials(),
                                   { Forward_renderer::Pass::edge_lines,
                                     Forward_renderer::Pass::hidden_line_with_blend
                                   },
                                   erhe::scene::Mesh::c_visibility_selection);
        gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
        //gl::disable(gl::Enable_cap::sample_alpha_to_one);
    }

    gl::enable(gl::Enable_cap::program_point_size);
    if (m_viewport_config && m_viewport_config->polygon_centroids)
    {
        ZoneScopedN("selection polygon centroids");
        m_forward_renderer->primitive_color_source   = Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_color = m_viewport_config->centroid_color;
        m_forward_renderer->primitive_constant_size  = m_viewport_config->point_size;
        m_forward_renderer->render(m_scene_viewport,
                                   m_scene_manager->camera(),
                                   m_scene_manager->selection_layers(),
                                   m_scene_manager->materials(),
                                   { Forward_renderer::Pass::polygon_centroids },
                                   erhe::scene::Mesh::c_visibility_selection);
    }
    if (m_viewport_config && m_viewport_config->corner_points)
    {
        ZoneScopedN("selection corner points");
        m_forward_renderer->primitive_color_source   = Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_color = m_viewport_config->corner_color;
        m_forward_renderer->primitive_constant_size  = m_viewport_config->point_size;
        m_forward_renderer->render(m_scene_viewport,
                                   m_scene_manager->camera(),
                                   m_scene_manager->selection_layers(),
                                   m_scene_manager->materials(),
                                   { Forward_renderer::Pass::corner_points },
                                   erhe::scene::Mesh::c_visibility_selection);
    }
    gl::disable(gl::Enable_cap::program_point_size);
}

void Editor::render_tool_meshes()
{
    ZoneScoped;

    m_forward_renderer->render(m_scene_viewport,
                               m_scene_manager->camera(),
                               m_scene_manager->tool_layers(),
                               m_scene_manager->materials(),
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

void Editor::render_update_tools()
{
    ZoneScoped;

    for (auto* tool : m_background_tools)
    {
        tool->render_update();
    }
    for (auto* tool : m_tools)
    {
        tool->render_update();
    }
}

void Editor::update_and_render_tools()
{
    ZoneScoped;

    Render_context render_context;
    render_context.pointer_context = &m_pointer_context;
    render_context.line_renderer   = m_line_renderer.get();
    render_context.text_renderer   = m_text_renderer.get();
    render_context.scene_manager   = m_scene_manager.get();
    render_context.viewport        = m_scene_viewport;
    render_context.time            = m_time;

    for (auto* tool : m_background_tools)
    {
        tool->update(m_pointer_context);
        tool->render(render_context);
    }
    for (auto* tool : m_tools)
    {
        tool->render(render_context);
    }
}

void Editor::render()
{
    ZoneScoped;

    int w = width();
    int h = height();
    if ((w == 0) || (h == 0))
    {
        return;
    }

    if (m_trigger_capture)
    {
        m_application->begin_renderdoc_capture();
    }

    begin_frame();

    render_update_tools();

    if ((m_scene_viewport.width  > 0) &&
        (m_scene_viewport.height > 0) &&
        is_primary_scene_output_framebuffer_ready())
    {
        if (m_scene_manager)
        {
            m_scene_manager->sort_lights();
            render_shadowmaps();

            if (m_id_renderer && m_pointer_context.pointer_in_content_area())
            {
                render_id();
            }
        }

        bind_primary_scene_output_framebuffer();
        render_clear_primary();

        if (m_scene_manager && m_forward_renderer)
        {
            if (m_viewport_config && m_viewport_config->polygon_fill)
            {
                render_content();
            }
            render_selection();
            render_tool_meshes();
        }

        gl::disable(gl::Enable_cap::framebuffer_srgb);

        update_pointer();
        update_and_render_tools();

        if (m_line_renderer)
        {
            m_line_renderer->render(m_scene_viewport, m_scene_manager->camera());
        }

        if (m_text_renderer)
        {
            m_text_renderer->render(m_scene_viewport);
        }

        if (m_shadow_renderer)  m_shadow_renderer ->next_frame();
        if (m_id_renderer)      m_id_renderer     ->next_frame();
        if (m_forward_renderer) m_forward_renderer->next_frame();
        if (m_text_renderer)    m_text_renderer   ->next_frame();
        if (m_line_renderer)    m_line_renderer   ->next_frame();

        end_primary_framebuffer();
    }

    if (m_enable_gui)
    {
        m_pointer_context.priority_action = m_priority_action;
        for (auto* window : m_windows)
        {
            window->window(m_pointer_context);
        }

        if (m_forward_renderer)
        {
            m_forward_renderer->debug_properties_window();
        }
        if (m_priority_action != m_pointer_context.priority_action)
        {
            set_priority_action(m_pointer_context.priority_action);
        }
        gui_render();
    }
}

auto Editor::to_scene_content(glm::vec2 position_in_root) -> glm::vec2
{
    if (m_enable_gui)
    {
        return m_viewport_window->to_scene_content(position_in_root);
    }
    else
    {
        return { position_in_root.x, height() - position_in_root.y };
    }
}

void Editor::update_pointer()
{
    VERIFY(m_scene_manager);

    glm::vec2 pointer = to_scene_content(glm::vec2(m_pointer_context.mouse_x, m_pointer_context.mouse_y));

    float z{1.0f};
    m_pointer_context.camera           = &m_scene_manager->camera();
    m_pointer_context.pointer_x        = static_cast<int>(pointer.x);
    m_pointer_context.pointer_y        = static_cast<int>(pointer.y);
    m_pointer_context.pointer_z        = z;
    m_pointer_context.viewport.x       = 0;
    m_pointer_context.viewport.y       = 0;
    m_pointer_context.viewport.width   = m_scene_viewport.width;
    m_pointer_context.viewport.height  = m_scene_viewport.height;
    m_pointer_context.scene_view_focus = m_enable_gui ? m_viewport_window->is_focused() : true;

    if (m_pointer_context.pointer_in_content_area() && m_id_renderer)
    {
        auto mesh_primitive = m_id_renderer->get(m_pointer_context.pointer_x, m_pointer_context.pointer_y, m_pointer_context.pointer_z);    
        m_pointer_context.hover_valid     = mesh_primitive.valid;
        if (m_pointer_context.hover_valid)
        {
            m_pointer_context.hover_mesh        = mesh_primitive.mesh;
            m_pointer_context.hover_layer       = mesh_primitive.layer;
            m_pointer_context.hover_primitive   = mesh_primitive.mesh_primitive_index;
            m_pointer_context.hover_local_index = mesh_primitive.local_index;
            m_pointer_context.hover_tool        = m_pointer_context.hover_layer == m_scene_manager->tool_layer().get();
            m_pointer_context.hover_content     = m_pointer_context.hover_layer == m_scene_manager->content_layer().get();
            if (m_pointer_context.hover_mesh != nullptr)
            {
                const auto& primitive = m_pointer_context.hover_mesh->primitives[m_pointer_context.hover_primitive];
                auto* geometry = primitive.primitive_geometry->source_geometry.get();
                m_pointer_context.hover_normal = {};
                if (geometry != nullptr)
                {
                    Polygon_id polygon_id = static_cast<Polygon_id>(m_pointer_context.hover_local_index);
                    if (polygon_id < geometry->polygon_count())
                    {
                        auto* polygon_normals = geometry->polygon_attributes().find<glm::vec3>(c_polygon_normals);
                        if ((polygon_normals != nullptr) && polygon_normals->has(polygon_id))
                        {
                            auto local_normal = polygon_normals->get(polygon_id);
                            auto world_from_node = m_pointer_context.hover_mesh->node->world_from_node();
                            m_pointer_context.hover_normal = glm::vec3(world_from_node * glm::vec4(local_normal, 0.0f));
                        }
                    }
                }
            }
             
        }
        // else mesh etc. contain latest valid values
    }
    else
    {
        m_pointer_context.hover_valid       = false;
        m_pointer_context.hover_mesh        = nullptr;
        m_pointer_context.hover_primitive   = 0;
        m_pointer_context.hover_local_index = 0;
    }
}

void Editor::on_enter()
{
    gl::clip_control(gl::Clip_control_origin::lower_left,
                     gl::Clip_control_depth::zero_to_one);
    gl::disable(gl::Enable_cap::primitive_restart);
    gl::enable(gl::Enable_cap::primitive_restart_fixed_index);
    gl::enable(gl::Enable_cap::texture_cube_map_seamless);
    m_current_time = chrono::steady_clock::now();
}

void Editor::on_key(bool pressed, erhe::toolkit::Keycode code, uint32_t modifier_mask)
{
    ImGuiIO& io = ImGui::GetIO();

    if (io.WantCaptureKeyboard && !m_viewport_window->is_focused() && pressed) {
        return;
    }

    m_pointer_context.shift   = (modifier_mask & Key_modifier_bit_shift) == Key_modifier_bit_shift;
    m_pointer_context.alt     = (modifier_mask & Key_modifier_bit_menu ) == Key_modifier_bit_menu;
    m_pointer_context.control = (modifier_mask & Key_modifier_bit_ctrl ) == Key_modifier_bit_ctrl;
    switch (code)
    {
        case Keycode::Key_f2:         m_trigger_capture = true; break;
        case Keycode::Key_space:      return m_fly_camera_tool->y_pos_control(pressed);
        case Keycode::Key_left_shift: return m_fly_camera_tool->y_neg_control(pressed);
        case Keycode::Key_w:          return m_fly_camera_tool->z_neg_control(pressed);
        case Keycode::Key_s:          return m_fly_camera_tool->z_pos_control(pressed);
        case Keycode::Key_a:          return m_fly_camera_tool->x_neg_control(pressed);
        case Keycode::Key_d:          return m_fly_camera_tool->x_pos_control(pressed);
        default: break;
    }
}

void Editor::on_mouse_click(erhe::toolkit::Mouse_button button, int count)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse && !m_viewport_window->is_focused() && (count > 0))
    {
        return;
    }

    m_pointer_context.mouse_button[button].pressed  = (count > 0);
    m_pointer_context.mouse_button[button].released = (count == 0);
    m_pointer_context.mouse_moved                   = false;

    on_pointer();
}

void Editor::on_mouse_move(double x, double y)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse && !m_viewport_window->is_focused())
    {
        return;
    }

    m_pointer_context.mouse_moved = true;
    m_pointer_context.mouse_x     = x;
    m_pointer_context.mouse_y     = y;
    update_pointer();
    on_pointer();
}

void Editor::cancel_ready_tools(Tool* keep)
{
    for (auto* tool : m_tools)
    {
        if (tool == keep)
        {
            continue;
        }
        if (tool->state() == Tool::State::ready)
        {
            log_input_events.trace("Canceling ready tool {}\n", tool->description());
            tool->cancel_ready();
            return;
        }
    }
}

auto Editor::get_action_tool(Action action) -> Tool*
{
    switch (action)
    {
        case Action::select:    return m_selection_tool.get();
        case Action::add:       return m_brushes.get();
        case Action::remove:    return m_brushes.get();
        case Action::translate: return m_trs_tool.get();
        case Action::rotate:    return m_trs_tool.get();
        default:
            return nullptr;
    }
}

auto Editor::get_priority_action() const -> Action
{
    return m_priority_action;
}

void Editor::set_priority_action(Action action)
{
    log_tools.trace("set_priority_action(action = {})\n", c_action_strings[static_cast<int>(action)]);
    m_priority_action = action;
    auto* tool = get_action_tool(action);

    switch (action)
    {
        case Action::translate:
        {
            m_trs_tool->set_rotate(false);
            m_trs_tool->set_translate(true);
            break;
        }
        case Action::rotate:
        {
            m_trs_tool->set_rotate(true);
            m_trs_tool->set_translate(false);
            break;
        }
        default:
        {
            m_trs_tool->set_rotate(false);
            m_trs_tool->set_translate(false);
            break;
        }
    }

    if (tool == nullptr)
    {
        log_tools.trace("tool not found\n");
        return;
    }
    auto i = std::find(m_tools.begin(), m_tools.end(), tool);
    if (i == m_tools.begin())
    {
        return;
    }
    if (i != m_tools.end())
    {
        std::swap(*i, *m_tools.begin());
        log_tools.trace("moved tool first\n");
        return;
    }
    log_tools.trace("tool not in list\n");
}

void Editor::on_pointer()
{
    // Pass 1: Active tools
    for (auto* tool : m_tools)
    {
        if ((tool->state() == Tool::State::active) && tool->update(m_pointer_context))
        {
            log_input_events.trace("Active tool {} consumed pointer event\n", tool->description());
            cancel_ready_tools(nullptr);
            return;
        }
    }

    // Pass 2: Ready tools
    for (auto* tool : m_tools)
    {
        if ((tool->state() == Tool::State::ready) && tool->update(m_pointer_context))
        {
            log_input_events.trace("Ready tool {} consumed pointer event\n", tool->description());
            cancel_ready_tools(nullptr);
            return;
        }
    }

    // Oass 3: Passive tools
    for (auto* tool : m_tools)
    {
        if ((tool->state() == Tool::State::passive) && tool->update(m_pointer_context))
        {
            log_input_events.trace("Passive tool {} consumed pointer event\n", tool->description());
            cancel_ready_tools(tool);
            return;
        }
    }
}

void Editor::bind_primary_scene_output_framebuffer()
{
    if (m_enable_gui)
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
void Editor::end_primary_framebuffer()
{
    if (m_enable_gui)
    {
        m_viewport_window->multisample_resolve();
    }
}

void Editor::register_tool(Tool* tool)
{
    m_tools.emplace_back(tool);
    Window* window = dynamic_cast<Window*>(tool);
    if (window != nullptr)
    {
        m_windows.emplace_back(window);
    }
}

void Editor::register_background_tool(Tool* tool)
{
    m_background_tools.emplace_back(tool);
    Window* window = dynamic_cast<Window*>(tool);
    if (window != nullptr)
    {
        m_windows.emplace_back(window);
    }
}

void Editor::register_window(Window* window)
{
    m_windows.emplace_back(window);
}

}
