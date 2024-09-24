#include "xr/headset_view.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "time.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "tools/tools.hpp"
#include "xr/controller_visualization.hpp"
#include "xr/hand_tracker.hpp"

#include "erhe_bit/bit_helpers.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/scoped_line_renderer.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"
#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window.hpp"
#include "erhe_xr/headset.hpp"
#include "erhe_xr/xr_instance.hpp"
#include "erhe_xr/xr_session.hpp"

#include <imgui/imgui.h>

namespace editor {

using erhe::graphics::Color_blend_state;

#pragma region Headset_camera_offset_move_command
Headset_camera_offset_move_command::Headset_camera_offset_move_command(erhe::commands::Commands& commands, erhe::math::Input_axis& variable, char axis)
    : Command   {commands, ""}
    , m_variable{variable}
    , m_axis    {axis}
{
}

auto Headset_camera_offset_move_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    m_variable.adjust(input.timestamp, input.variant.float_value);
    return true;
}

#pragma endregion Fly_camera_variable_float_command

Headset_view_node::Headset_view_node(erhe::rendergraph::Rendergraph& rendergraph, Headset_view& headset_view)
    : erhe::rendergraph::Rendergraph_node{rendergraph, "Headset"}
    , m_headset_view                     {headset_view}
{
    register_input(erhe::rendergraph::Routing::Resource_provided_by_producer, "shadow_maps", erhe::rendergraph::Rendergraph_node_key::shadow_maps);
    register_input(erhe::rendergraph::Routing::Resource_provided_by_producer, "rendertarget texture", erhe::rendergraph::Rendergraph_node_key::rendertarget_texture);
}

void Headset_view_node::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION();

    m_headset_view.render_headset();
}

Headset_view::Headset_view(
    erhe::commands::Commands&       commands,
    erhe::graphics::Instance&       graphics_instance,
    erhe::imgui::Imgui_renderer&    imgui_renderer,
    erhe::imgui::Imgui_windows&     imgui_windows,
    erhe::rendergraph::Rendergraph& rendergraph,
    erhe::window::Context_window&   context_window,
    Editor_context&                 editor_context,
    Editor_rendering&               editor_rendering,
    Editor_settings&                editor_settings,
    Time&                           time
)
    : Scene_view               {editor_context, Viewport_config::default_config()}
    , Update_time_base         {time}
    , erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Headset", "headset"}
    , m_translate_x     {"translate_x"}
    , m_translate_y     {"translate_y"}
    , m_translate_z     {"translate_z"}
    , m_offset_x_command{commands, m_translate_x, 'x'}
    , m_offset_y_command{commands, m_translate_y, 'y'}
    , m_offset_z_command{commands, m_translate_z, 'z'}
    , m_editor_context  {editor_context}
    , m_context_window  {context_window}
{
    ERHE_PROFILE_FUNCTION();

    if (!editor_context.OpenXR) {
        return;
    }

    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "headset");
    ini.get("quad_view",         config.quad_view);
    ini.get("debug",             config.debug);
    ini.get("depth",             config.depth);
    ini.get("visibility_mask",   config.visibility_mask);
    ini.get("hand_tracking",     config.hand_tracking);
    ini.get("composition_alpha", config.composition_alpha);

    editor_rendering.add(this);

    commands.register_command(&m_offset_x_command);
    commands.register_command(&m_offset_y_command);
    commands.register_command(&m_offset_z_command);
    commands.bind_command_to_controller_axis(&m_offset_x_command, 0);
    commands.bind_command_to_controller_axis(&m_offset_y_command, 2);
    commands.bind_command_to_controller_axis(&m_offset_z_command, 1);
    m_translate_x.set_power_base(4.0f);
    m_translate_y.set_power_base(4.0f);
    m_translate_z.set_power_base(4.0f);

    const erhe::xr::Xr_configuration configuration{
        .debug             = config.debug,
        .quad_view         = config.quad_view,
        .depth             = config.depth,
        .visibility_mask   = config.visibility_mask,
        .hand_tracking     = config.hand_tracking,
        .composition_alpha = config.composition_alpha,
        .mirror_mode       = editor_context.OpenXR_mirror
    };

    {
        ERHE_PROFILE_SCOPE("make xr::Headset");

        m_headset = std::make_unique<erhe::xr::Headset>(context_window, configuration );
        if (!m_headset->is_valid()) {
            log_headset->info("Headset initialization failed. Disabling OpenXR.");
            m_context.OpenXR = false;
            m_headset.reset();
            return;
        }
    }

    m_rendergraph_node = std::make_shared<Headset_view_node>(rendergraph, *this);

    m_shadow_render_node = editor_rendering.create_shadow_node_for_scene_view(graphics_instance, rendergraph, editor_settings, *this);
    rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::shadow_maps, m_shadow_render_node.get(), m_rendergraph_node.get());
}

void Headset_view::attach_to_scene(std::shared_ptr<Scene_root> scene_root, Mesh_memory& mesh_memory)
{
    m_scene_root = scene_root;

    setup_root_camera();

    {
        ERHE_PROFILE_SCOPE("make Controller_visualization");

        m_controller_visualization = std::make_unique<Controller_visualization>(
            m_root_node.get(),
            mesh_memory,
            *m_scene_root.get()
        );
    }

}

void Headset_view::update_once_per_frame(const Time_context& time_context)
{
    m_translate_x.update(time_context.timestamp);
    m_translate_y.update(time_context.timestamp);
    m_translate_z.update(time_context.timestamp);
    const float tx =  m_translate_x.get_tick_distance();
    const float ty = -m_translate_y.get_tick_distance();
    const float tz =  m_translate_z.get_tick_distance();
    if (tx != 0.0f || ty != 0.0f || tz != 0.0f) {
        const glm::vec3 translation{tx, ty, tz};
        m_camera_offset += translation;
    }
}

void Headset_view::imgui()
{
    // Scene selection
    auto                        old_scene_root = m_scene_root;
    std::shared_ptr<Scene_root> scene_root     = get_scene_root();
    Scene_root*                 scene_root_raw = scene_root.get();
    const bool combo_used = m_editor_context.editor_scenes->scene_combo("##Scene", scene_root_raw, false);
    if (combo_used) {
        scene_root = (scene_root_raw != nullptr) 
            ? scene_root_raw->shared_from_this()
            : std::shared_ptr<Scene_root>{};
        m_scene_root = scene_root;
    }

    // Shader selection
    ImGui::Combo(
        "Render Mode",
        reinterpret_cast<int*>(&m_shader_stages_variant),
        c_shader_stages_variant_strings,
        IM_ARRAYSIZE(c_shader_stages_variant_strings),
        IM_ARRAYSIZE(c_shader_stages_variant_strings)
    );

}

void Headset_view::render(const Render_context& render_context)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_context.OpenXR) {
        return;
    }

    // TODO Handle selection stencil
    erhe::renderer::Scoped_line_renderer line_renderer = render_context.get_line_renderer(2, true, true);

    constexpr glm::vec4 red   {1.0f, 0.0f, 0.0f, 1.0f};
    constexpr glm::vec4 green {0.0f, 1.0f, 0.0f, 1.0f};
    constexpr glm::vec4 blue  {0.0f, 0.0f, 1.0f, 1.0f};
    constexpr glm::vec4 cyan  {0.0f, 1.0f, 0.0f, 1.0f};
    constexpr glm::vec4 orange{1.0f, 0.8f, 0.5f, 0.5f};

    line_renderer.set_thickness(4.0f);
    for (const auto& finger_input : m_finger_inputs) {
        const glm::vec4 color = green;
        line_renderer.set_line_color(color);
        line_renderer.add_lines({{finger_input.finger_point, finger_input.point}});
    }

    erhe::xr::Xr_action_pose* left_aim_pose = m_headset->get_actions_right().aim_pose;
    erhe::xr::Xr_action_pose* right_aim_pose = m_headset->get_actions_right().aim_pose;
    const bool can_use_left  = (left_aim_pose  != nullptr) && left_aim_pose->location.locationFlags != 0;
    const bool can_use_right = (right_aim_pose != nullptr) && right_aim_pose->location.locationFlags != 0;
    const auto* pose = can_use_right 
        ? m_headset->get_actions_right().aim_pose 
        : can_use_left 
            ? m_headset->get_actions_left().aim_pose
            : nullptr;

    if (pose != nullptr) {
        const auto* trigger_value_action = (pose == right_aim_pose) 
            ? m_headset->get_actions_right().trigger_value 
            : m_headset->get_actions_left().trigger_value;
        const auto* click_action = (pose == right_aim_pose) 
            ? m_headset->get_actions_right().a_click
            : m_headset->get_actions_left().x_click;
        const float trigger_value = (trigger_value_action != nullptr) ? trigger_value_action->state.currentState : 0.0f;
        const bool click = (click_action != nullptr) && (click_action->state.currentState == XR_TRUE);

        const auto* nearest = get_nearest_hover(Hover_entry::all_bits);
        bool use_hover = 
            (nearest != nullptr) &&
            nearest->position.has_value() &&
            get_control_ray_origin_in_world().has_value() &&
            get_control_ray_direction_in_world().has_value();

        const auto position = use_hover
            ? get_control_ray_origin_in_world().value()
            : pose->position + get_camera_offset();

        const auto orientation = glm::mat4_cast(pose->orientation);

        const auto direction = use_hover
            ? get_control_ray_direction_in_world().value() 
            : glm::vec3{orientation * glm::vec4{0.0f, 0.0f, -1.0f, 0.0f}};

        const auto tip = use_hover
            ? nearest->position.value() 
            : position + trigger_value * direction;

        bool is_content      = (nearest != nullptr) && erhe::bit::test_all_rhs_bits_set(nearest->mask, Hover_entry::content_bit);
        bool is_tool         = (nearest != nullptr) && erhe::bit::test_all_rhs_bits_set(nearest->mask, Hover_entry::tool_bit);
        bool is_rendertarget = (nearest != nullptr) && erhe::bit::test_all_rhs_bits_set(nearest->mask, Hover_entry::rendertarget_bit);
        glm::vec4 type_color =
            is_content      ? glm::vec4{0.8f, 0.5f, 0.3f, 1.0f} :
            is_tool         ? glm::vec4{1.0f, 0.0f, 1.0f, 1.0f} :
            is_rendertarget ? glm::vec4{0.6f, 0.6f, 0.6f, 1.0f} :
                              glm::vec4{0.4f, 0.4f, 0.4f, 1.0f};
        glm::vec4 near_color = click ? glm::vec4{1.0f, 1.0f, 1.0f, 1.0f} : type_color;
        glm::vec4 far_color  = glm::vec4{type_color.x, type_color.y, type_color.z, 0.4f};
        line_renderer.add_line(
            near_color, 0.0f, position,
            near_color, 2.0f, tip
        );
        if ((trigger_value < 1.0f) && !is_rendertarget) {
            line_renderer.add_line(
                far_color, 2.0f, tip,
                far_color, 8.0f, position + 100.0f * direction
            );
        }
    }
}

auto Headset_view::get_headset_view_resources(erhe::xr::Render_view& render_view) -> std::shared_ptr<Headset_view_resources>
{
    ERHE_PROFILE_FUNCTION();

    auto match_color_texture = [&render_view](const auto& i) {
        return i->get_color_texture()->gl_name() == render_view.color_texture;
    };

    const auto i = std::find_if(m_view_resources.begin(), m_view_resources.end(), match_color_texture);
    if (i == m_view_resources.end()) {
        auto resource = std::make_shared<Headset_view_resources>(
            *m_context.graphics_instance,
            render_view,                               // erhe::xr::Render_view& render_view,
            *this,                                     // Headset_view&          headset_view,
            static_cast<std::size_t>(render_view.slot) // const std::size_t      slot
        );
        const std::string label = fmt::format(
            "Headset Viewport_scene_view slot {} color texture {}",
            render_view.slot,
            render_view.color_texture
        );
        m_view_resources.push_back(resource);
        return resource;
    }
    return *i;
}

static constexpr std::string_view c_id_headset_clear{"HS clear"};
static constexpr std::string_view c_id_headset_render_content{"HS render content"};

void Headset_view::update_pointer_context_from_controller()
{
    ERHE_PROFILE_FUNCTION();

    auto* left_aim_pose  = m_headset->get_actions_left().aim_pose;
    auto* right_aim_pose = m_headset->get_actions_right().aim_pose;
    auto* pose =
        (
            (right_aim_pose != nullptr) &&
            (right_aim_pose->location.locationFlags != 0)
        )
            ? right_aim_pose
            : left_aim_pose;

    if (pose == nullptr) {
        return;
    }

    // TODO optimize this transform computation
    const glm::mat4 orientation = glm::mat4_cast(pose->orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{1}, pose->position + get_camera_offset());
    const glm::mat4 m           = translation * orientation;

    this->Scene_view::set_world_from_control(m);
    this->Scene_view::update_hover_with_raytrace();
    this->Scene_view::update_grid_hover();
}

void Headset_view::render_headset()
{
    ERHE_PROFILE_FUNCTION();

    if (m_headset == nullptr) {
        return;
    }

    auto frame_timing = m_headset->begin_frame_();
    if (!frame_timing.begin_ok) {
        return;
    }

    if (frame_timing.should_render) {
        bool first_view = true;
        auto callback = [this, &first_view](erhe::xr::Render_view& render_view) -> bool {
            const auto& view_resources = get_headset_view_resources(render_view);
            if (!view_resources->is_valid()) {
                return false;
            }

            if (m_request_renderdoc_capture) {
                erhe::window::start_frame_capture(m_context_window);
                m_renderdoc_capture_started = true;
                m_request_renderdoc_capture = false;
            }

            erhe::scene::Projection::Fov_sides fov_sides{render_view.fov_left, render_view.fov_right, render_view.fov_up, render_view.fov_down};
            if (m_context.OpenXR_mirror) {
                erhe::window::Context_window* window = m_context.context_window;
                //constexpr float near_value = 1.0f;
                float left_over_near  = std::tan(render_view.fov_left );
                float right_over_near = std::tan(render_view.fov_right);
                float up_over_near    = std::tan(render_view.fov_up   );
                float down_over_near  = std::tan(render_view.fov_down );
                float left            = /* near_value * */ left_over_near;
                float right           = /* near_value * */ right_over_near;
                float up              = /* near_value * */ up_over_near;
                float down            = /* near_value * */ down_over_near;
                float desired_aspect  = static_cast<float>(window->get_width()) / static_cast<float>(window->get_height());
                // (expansion + right - left) / (up - down) = desired_aspect   || * (up - down)
                // expansion + right - left = desired_aspect * (up - down)     || -right +left
                // expansion = desired_aspect * (up - donw) - right + left
                float expansion       = desired_aspect * (up - down) - right + left;
                float expanded_left   = left - 0.5f * expansion;
                float expanded_right  = right + 0.5f * expansion;
                erhe::scene::Projection::Fov_sides expanded_fov_sides{
                    std::atan(expanded_left),
                    std::atan(expanded_right),
                    render_view.fov_up,
                    render_view.fov_down
                };
                view_resources->update(render_view, expanded_fov_sides);
            } else {
                view_resources->update(render_view, fov_sides);
            }

            // Update scene node transforms
            // TODO can we do most of this only once per frame, not for each view?
            {
#if 1
                std::shared_ptr<Scene_root> scene_root = get_scene_root();
                ERHE_VERIFY(scene_root);
                scene_root->get_scene().update_node_transforms();
#endif
                m_context.tools->get_tool_scene_root()->get_hosted_scene()->update_node_transforms();

                // TODO Consider multiple scene view being able to be (hover) active
                //      (viewport window and headset view).
                m_context.editor_message_bus->send_message(
                    Editor_message{
                        .update_flags = Message_flag_bit::c_flag_bit_hover_scene_view | Message_flag_bit::c_flag_bit_render_scene_view,
                        .scene_view   = this
                    }
                );
            }

            auto& graphics_instance = *m_context.graphics_instance;
            auto* openxr_framebuffer = view_resources->get_framebuffer();
            erhe::math::Viewport viewport;

            if (m_context.OpenXR_mirror) {
                gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
                viewport = erhe::math::Viewport{
                    .x             = 0,
                    .y             = 0,
                    .width         = m_context.context_window->get_width(),
                    .height        = m_context.context_window->get_height(),
                    .reverse_depth = graphics_instance.configuration.reverse_depth
                };
            } else {
                gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, openxr_framebuffer->gl_name());

                auto status = gl::check_named_framebuffer_status(openxr_framebuffer->gl_name(), gl::Framebuffer_target::draw_framebuffer);
                if (status != gl::Framebuffer_status::framebuffer_complete) {
                    log_headset->error("OpenXR framebuffer status = {}", gl::c_str(status));
                }
                viewport = erhe::math::Viewport{
                    .x             = 0,
                    .y             = 0,
                    .width         = static_cast<int>(render_view.width),
                    .height        = static_cast<int>(render_view.height),
                    .reverse_depth = graphics_instance.configuration.reverse_depth
                };
            }

            graphics_instance.opengl_state_tracker.shader_stages.reset();
            graphics_instance.opengl_state_tracker.color_blend.execute(Color_blend_state::color_blend_disabled);

            bool render = !m_context.OpenXR_mirror || first_view;

            // TODO Meta link with Quest 3 does not seem to support camera video passthrough :(
            //      Also, this conflicts with hud grab
            //
            // const auto* squeeze_click = m_headset->get_actions_right().squeeze_click;
            // const auto* squeeze_value = m_headset->get_actions_right().squeeze_value;
            // if (
            //     ((squeeze_click != nullptr) && (squeeze_click->state.currentState == XR_TRUE)) ||
            //     ((squeeze_value != nullptr) && (squeeze_value->state.currentState >= 0.5f))
            // ) {
            //     gl::clear_color(0.0f, 0.0f, 0.0f, 0.0f);
            //     gl::clear(gl::Clear_buffer_mask::color_buffer_bit);
            //     render = false;
            // }

            if (render) {
                gl::viewport(viewport.x, viewport.y, viewport.width, viewport.height);
                gl::enable(gl::Enable_cap::framebuffer_srgb);

                gl::clear_color(0.0f, 0.0f, 0.0f, 0.0f);
                gl::clear_depth_f(*graphics_instance.depth_clear_value_pointer());
                gl::clear_stencil(0);
                gl::clear(gl::Clear_buffer_mask::color_buffer_bit | gl::Clear_buffer_mask::depth_buffer_bit | gl::Clear_buffer_mask::stencil_buffer_bit);

                //Viewport_config viewport_config;
                const erhe::graphics::Shader_stages* override_shader_stages = m_context.programs->get_variant_shader_stages(m_shader_stages_variant);
                Render_context render_context {
                    .editor_context         = m_context,
                    .scene_view             = *this,
                    .viewport_config        = m_viewport_config,
                    .camera                 = view_resources->get_camera(),
                    .viewport               = viewport,
                    .override_shader_stages = override_shader_stages
                };

                m_context.editor_rendering->render_composer(render_context);
                m_context.line_renderer   ->begin();
                m_context.tools           ->render_viewport_tools(render_context);
                m_context.editor_rendering->render_viewport_renderables(render_context);
                m_context.line_renderer   ->end();
                m_context.line_renderer   ->render(render_context.viewport, *render_context.camera);

                if (m_renderdoc_capture_started) {
                    erhe::window::end_frame_capture(m_context_window);
                    m_renderdoc_capture_started = false;
                }

                if (m_context.OpenXR_mirror) {
                    int src_width  = m_context.context_window->get_width();
                    int src_height = m_context.context_window->get_height();
                    int dst_width  = view_resources->get_width();
                    int dst_height = view_resources->get_height();

                    // - fit all one to one if possible
                    // - pad if not enough in src
                    // - crop if too much in src
                    int width_diff = std::abs(src_width - dst_width);
                    int src_x0 = (src_width > dst_width ) ? width_diff / 2     : 0;
                    int src_x1 = (src_width > dst_width ) ? src_x0 + dst_width : src_width;
                    int dst_x0 = (src_width > dst_width ) ? 0                  : width_diff / 2;
                    int dst_x1 = (src_width > dst_width ) ? dst_width          : dst_x0 + src_width;

                    int height_diff = std::abs(src_height - dst_height);
                    int src_y0 = (src_height > dst_height) ? height_diff / 2     : 0;
                    int src_y1 = (src_height > dst_height) ? src_y0 + dst_height : src_height;
                    int dst_y0 = (src_height > dst_height) ? 0                   : height_diff / 2;
                    int dst_y1 = (src_height > dst_height) ? dst_height          : dst_y0 + src_height;
                    gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, 0);
                    gl::read_buffer(gl::Read_buffer_mode::back);
                    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, openxr_framebuffer->gl_name());
                    gl::disable(gl::Enable_cap::framebuffer_srgb);
                    gl::blit_framebuffer(
                        src_x0, src_y0, src_x1, src_y1, 
                        dst_x0, dst_y0, dst_x1, dst_y1,
                        gl::Clear_buffer_mask::color_buffer_bit, gl::Blit_framebuffer_filter::nearest
                    );
                    gl::enable(gl::Enable_cap::framebuffer_srgb);
                    m_context_window.swap_buffers();
#if 0
                    int src_width  = view_resources->get_width();
                    int src_height = view_resources->get_height();
                    int dst_width  = m_context.context_window->get_width();
                    int dst_height = m_context.context_window->get_height();

                    // - fit all one to one if possible
                    // - pad if not enough in src
                    // - crop if too much in src
                    int width_diff = std::abs(src_width - dst_width);
                    int src_x0 = (src_width > dst_width ) ? width_diff / 2     : 0;
                    int src_x1 = (src_width > dst_width ) ? src_x0 + dst_width : src_width;
                    int dst_x0 = (src_width > dst_width ) ? 0                  : width_diff / 2;
                    int dst_x1 = (src_width > dst_width ) ? dst_width          : dst_x0 + src_width;

                    int height_diff = std::abs(src_height - dst_height);
                    int src_y0 = (src_height > dst_height) ? height_diff / 2     : 0;
                    int src_y1 = (src_height > dst_height) ? src_y0 + dst_height : src_height;
                    int dst_y0 = (src_height > dst_height) ? 0                   : height_diff / 2;
                    int dst_y1 = (src_height > dst_height) ? dst_height          : src_y0 + src_height;
                    gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, openxr_framebuffer->gl_name());
                    gl::read_buffer(gl::Read_buffer_mode::color_attachment0);
                    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
                    gl::blit_framebuffer(
                        src_x0, src_y0, src_x1, src_y1, 
                        dst_x0, dst_y0, dst_x1, dst_y1,
                        gl::Clear_buffer_mask::color_buffer_bit, gl::Blit_framebuffer_filter::nearest
                    );
                    gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, 0);
                    m_context_window.swap_buffers();
#endif
                }
                first_view = false;
            }

            return true;
        };
        m_headset->render(callback);
        ++m_frame_number;
    }

    m_headset->end_frame(frame_timing.should_render);
}

void Headset_view::setup_root_camera()
{
    ERHE_PROFILE_FUNCTION();

    const glm::mat4 m = erhe::math::create_look_at(
        glm::vec3{0.0f, 0.0f,  0.0f}, // eye
        glm::vec3{0.0f, 0.0f, -1.0f}, // look at
        glm::vec3{0.0f, 1.0f,  0.0f}  // up
    );

    m_root_node = m_scene_root->get_scene().get_root_node();
    m_headset_node = std::make_shared<erhe::scene::Node>("Headset Root Node");
    m_headset_node->set_parent(m_root_node);
    m_headset_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::visible);

    m_root_camera = std::make_shared<erhe::scene::Camera>("Root Camera");
    m_root_camera->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::visible);
    auto& projection = *m_root_camera->projection();
    projection.fov_y           = glm::radians(35.0f);
    projection.projection_type = erhe::scene::Projection::Type::perspective_vertical;
    projection.z_near          = 0.03f;
    projection.z_far           = 200.0f;
    m_headset_node->attach(m_root_camera);
}

void Headset_view::update_camera_node()
{
    glm::vec3 position{};
    glm::quat orientation{};
    if (!m_headset->get_headset_pose(position, orientation)) {
        return;
    }
    const glm::mat4 m_orientation   = glm::mat4_cast(orientation);
    const glm::mat4 m_translation   = glm::translate(glm::mat4{1}, position + get_camera_offset());
    const glm::mat4 world_from_view = m_translation * m_orientation;
    m_headset_node->set_world_from_node(world_from_view);
    m_headset_node->update_transform(0); // TODO
}

auto Headset_view::get_camera_offset() const -> glm::vec3
{
    return m_camera_offset;
}

auto Headset_view::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    ERHE_VERIFY(m_scene_root);
    return m_scene_root;
}

auto Headset_view::get_root_node() const -> std::shared_ptr<erhe::scene::Node>
{
    ERHE_VERIFY(m_scene_root);
    return m_root_node;
}

auto Headset_view::is_active() const -> bool
{
    return m_headset && m_headset->is_active();
}

auto Headset_view::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_root_camera;
}

auto Headset_view::get_rendergraph_node() -> erhe::rendergraph::Rendergraph_node*
{
    return m_rendergraph_node.get();
}

auto Headset_view::get_shadow_render_node() const -> Shadow_render_node*
{
    return m_shadow_render_node.get();
}

void Headset_view::add_finger_input(const Finger_point& finger_input)
{
    m_finger_inputs.push_back(finger_input);
}

auto Headset_view::finger_to_viewport_distance_threshold() const -> float
{
    return m_finger_to_viewport_distance_threshold;
}

auto Headset_view::get_headset() const -> erhe::xr::Headset*
{
    return m_headset.get();
}

auto Headset_view::update_events() -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!m_headset) {
        return false;
    }

    m_finger_inputs.clear();

    if (!m_headset->update_events()) {
        return false;
    }

    update_camera_node();

    update_pointer_context_from_controller();
    auto& instance = m_headset->get_xr_instance();
    const XrSession xr_session = m_headset->get_xr_session().get_xr_session();

    // Inject XR input events
    std::vector<erhe::window::Input_event>& input_events = m_context.context_window->get_input_events();
    for (auto& action : instance.get_boolean_actions()) {
        action.get(xr_session);
        if (action.state.changedSinceLastSync == XR_TRUE) {
            input_events.push_back(
                erhe::window::Input_event{
                    .type = erhe::window::Input_event_type::xr_boolean_event,
                    .u = {
                        .xr_boolean_event = {
                            .action = &action,
                            .value  = action.state.currentState == XR_TRUE
                        }
                    }
                }
            );
        }
    }
    for (auto& action : instance.get_float_actions()) {
        action.get(xr_session);
        if (action.state.changedSinceLastSync == XR_TRUE) {
            input_events.push_back(
                erhe::window::Input_event{
                    .type = erhe::window::Input_event_type::xr_float_event,
                    .u = {
                        .xr_float_event = {
                            .action = &action,
                            .value  = action.state.currentState
                        }
                    }
                }
            );
        }
    }
    for (auto& action : instance.get_vector2f_actions()) {
        action.get(xr_session);
        if (action.state.changedSinceLastSync == XR_TRUE) {
            input_events.push_back(
                erhe::window::Input_event{
                    .type = erhe::window::Input_event_type::xr_vector2f_event,
                    .u = {
                        .xr_vector2f_event = {
                            .action = &action,
                            .x = action.state.currentState.x,
                            .y = action.state.currentState.y
                        }
                    }
                }
            );
        }
    }

    if (m_controller_visualization) {
        // TODO both controllers
        auto* left_aim_pose  = m_headset->get_actions_left().aim_pose;
        auto* right_aim_pose = m_headset->get_actions_right().aim_pose;
        auto* pose = ((right_aim_pose != nullptr) && (right_aim_pose->location.locationFlags != 0)) ? right_aim_pose : left_aim_pose;
        if (pose != nullptr) {
            erhe::xr::Xr_action_pose pose_with_offset = *pose;
            pose_with_offset.position += get_camera_offset();
            m_controller_visualization->update(&pose_with_offset);
        }
    }
    //if (m_hand_tracker)
    //{
    //    m_hand_tracker->update_hands(*m_headset.get());
    //}
    return true;
}

void Headset_view::request_renderdoc_capture()
{
    m_request_renderdoc_capture = true;
}

void Headset_view::end_frame()
{
    if (!m_headset) {
        return;
    }

    m_finger_inputs.clear();

    // TODO These should be done when session state changes so that polling no longer happens
    auto& instance = m_headset->get_xr_instance();
    for (auto& action : instance.get_boolean_actions()) {
        action.state.changedSinceLastSync = XR_FALSE;
    }
    for (auto& action : instance.get_float_actions()) {
        action.state.changedSinceLastSync = XR_FALSE;
    }
    for (auto& action : instance.get_vector2f_actions()) {
        action.state.changedSinceLastSync = XR_FALSE;
    }
}

} // namespace editor

