#include "xr/headset_view.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "renderers/shadow_renderer.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "rendertarget_imgui_viewport.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "scene/material_library.hpp"
#include "tools/hotbar.hpp"
#include "tools/hud.hpp"
#include "tools/tools.hpp"
#include "xr/controller_visualization.hpp"
#include "xr/hand_tracker.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/window_imgui_viewport.hpp"
#include "erhe/application/window.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/gl/enum_bit_mask_operators.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/xr/headset.hpp"
#include "erhe/xr/xr_instance.hpp"
#include "erhe/xr/xr_session.hpp"

#include <imgui.h>

namespace editor
{

using erhe::graphics::Color_blend_state;

Headset_view_node::Headset_view_node()
    : erhe::application::Rendergraph_node{"Headset"}
{
    register_input(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        "shadow_maps",
        erhe::application::Rendergraph_node_key::shadow_maps
    );
    register_input(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        "rendertarget texture",
        erhe::application::Rendergraph_node_key::rendertarget_texture
    );
}

void Headset_view_node::execute_rendergraph_node()
{
    g_headset_view->render_headset();
}

Headset_view* g_headset_view{nullptr};

Headset_view::Headset_view()
    : erhe::components::Component    {c_name}
    , erhe::application::Imgui_window{c_description}
{
}

Headset_view::~Headset_view() noexcept
{
    ERHE_VERIFY(g_headset_view == nullptr);
}

void Headset_view::deinitialize_component()
{
    ERHE_VERIFY(g_headset_view == this);
    m_rendergraph_node.reset();
    m_shadow_render_node.reset();
    m_scene_root.reset();
    m_headset.reset();
    m_root_node.reset();
    m_root_camera.reset();
    m_view_resources.clear();
    m_controller_visualization.reset();
    m_finger_inputs.clear();
    g_headset_view = nullptr;
}

void Headset_view::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<erhe::application::Imgui_windows>();
    require<erhe::application::Window>();
    require<Mesh_memory    >();
    require<Scene_builder  >();
    require<Shadow_renderer>();
    require<Tools          >();
}

void Headset_view::initialize_component()
{
    ERHE_PROFILE_FUNCTION
    ERHE_VERIFY(g_headset_view == nullptr);
    g_headset_view = this; // we have early out

    auto ini = erhe::application::get_ini("erhe.ini", "headset");
    ini->get("openxr",            config.openxr);
    ini->get("quad_view",         config.quad_view);
    ini->get("debug",             config.debug);
    ini->get("depth",             config.depth);
    ini->get("visibility_mask",   config.visibility_mask);
    ini->get("hand_tracking",     config.hand_tracking);
    ini->get("composition_alpha", config.composition_alpha);

    erhe::application::g_imgui_windows->register_imgui_window(this, "headset_view");

    m_scene_root = g_scene_builder->get_scene_root();

    setup_root_camera();

    if (!config.openxr)
    {
        return;
    }

    const erhe::xr::Xr_configuration configuration
    {
        .debug             = config.debug,
        .quad_view         = config.quad_view,
        .depth             = config.depth,
        .visibility_mask   = config.visibility_mask,
        .hand_tracking     = config.hand_tracking,
        .composition_alpha = config.composition_alpha
    };

    {
        ERHE_PROFILE_SCOPE("make xr::Headset");

        m_headset = std::make_unique<erhe::xr::Headset>(
            erhe::application::g_window->get_context_window(),
            configuration
        );
        if (!m_headset->is_valid())
        {
            log_headset->info("Headset not initialized");
            m_headset.reset();
            return;
        }
    }

    {
        ERHE_PROFILE_SCOPE("make Controller_visualization");

        m_controller_visualization = std::make_unique<Controller_visualization>(
            *m_scene_root,
            m_root_node.get()
        );
    }

    m_rendergraph_node = std::make_shared<Headset_view_node>();
    erhe::application::g_rendergraph->register_node(m_rendergraph_node);

    set_flags      (Tool_flags::background);
    set_description(c_description);
    g_tools->register_tool(this);

    m_shadow_render_node = g_shadow_renderer->create_node_for_scene_view(*this);
    erhe::application::g_rendergraph->connect(
        erhe::application::Rendergraph_node_key::shadow_maps,
        m_shadow_render_node,
        m_rendergraph_node
    );
}

void Headset_view::post_initialize()
{
    // Move all imgui windows that have window viewport to hud viewport
    const auto viewport = g_hud->get_rendertarget_imgui_viewport();
    const auto window_viewport = erhe::application::g_imgui_windows->get_window_viewport();
    if (viewport)
    {
        auto& windows = erhe::application::g_imgui_windows->get_windows();
        for (auto window : windows)
        {
            if (window->get_viewport() == window_viewport.get())
            {
                window->set_viewport(viewport.get());
            }
        }
    }
}

void Headset_view::tool_render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

    static_cast<void>(context);

    auto& line_renderer = *erhe::application::g_line_renderer_set->visible.at(2).get();

    constexpr glm::vec4 red   {1.0f, 0.0f, 0.0f, 1.0f};
    constexpr glm::vec4 green {0.0f, 1.0f, 0.0f, 1.0f};
    constexpr glm::vec4 blue  {0.0f, 0.0f, 1.0f, 1.0f};
    constexpr glm::vec4 cyan  {0.0f, 1.0f, 0.0f, 1.0f};
    constexpr glm::vec4 orange{1.0f, 0.8f, 0.5f, 0.5f};

    line_renderer.set_thickness(4.0f);
    for (const auto& finger_input : m_finger_inputs)
    {
        const glm::vec4 color = m_mouse_down
            ? green
            : red;
        line_renderer.set_line_color(color);
        line_renderer.add_lines({{finger_input.finger_point, finger_input.point}});
    }

    {
        const bool use_right = (m_headset->get_actions_right().aim_pose->location.locationFlags != 0);
        const auto* pose = use_right
                ? m_headset->get_actions_right().aim_pose
                : m_headset->get_actions_left().aim_pose;

        if (pose != nullptr)
        {
            const auto* trigger_value_action = use_right
                    ? m_headset->get_actions_right().trigger_value
                    : m_headset->get_actions_left().trigger_value;
            const float trigger_value = (trigger_value_action != nullptr)
                ? trigger_value_action->state.currentState
                : 0.0f;
            const auto position    = pose->position;
            const auto orientation = glm::mat4_cast(pose->orientation);
            const auto direction   = glm::vec3{orientation * glm::vec4{0.0f, 0.0f, -1.0f, 0.0f}};

            line_renderer.add_lines(
                m_mouse_down ? cyan : blue,
                {
                    {
                        position,
                        position + trigger_value * direction
                    }
                }
            );
            if (trigger_value < 1.0f)
            {
                line_renderer.add_lines(
                    orange,
                    {
                        {
                            position + trigger_value * direction,
                            position + 100.0f * direction
                        }
                    }
                );
            }
        }
    }
}

auto Headset_view::get_headset_view_resources(
    erhe::xr::Render_view& render_view
) -> std::shared_ptr<Headset_view_resources>
{
    ERHE_PROFILE_FUNCTION

    auto match_color_texture = [&render_view](const auto& i)
    {
        return i->color_texture->gl_name() == render_view.color_texture;
    };

    const auto i = std::find_if(
        m_view_resources.begin(),
        m_view_resources.end(),
        match_color_texture
    );

    if (i == m_view_resources.end())
    {
        auto resource = std::make_shared<Headset_view_resources>(
            render_view,                               // erhe::xr::Render_view& render_view,
            *this,                                     // Headset_view&          headset_view,
            static_cast<std::size_t>(render_view.slot) // const std::size_t      slot
        );
        const std::string label = fmt::format(
            "Headset Viewport_window slot {} color texture {}",
            render_view.slot,
            render_view.color_texture
        );
        resource->viewport_window = std::make_shared<Viewport_window>(
            label,
            m_scene_root,
            resource->camera
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
    ERHE_PROFILE_FUNCTION

    auto* left_aim_pose  = m_headset->get_actions_left().aim_pose;
    auto* right_aim_pose = m_headset->get_actions_right().aim_pose;
    auto* pose =
        (
            (right_aim_pose != nullptr) &&
            (right_aim_pose->location.locationFlags != 0)
        )
            ? right_aim_pose
            : left_aim_pose;

    if (pose == nullptr)
    {
        return;
    }

    // TODO optimize this transform computation
    const glm::mat4 orientation = glm::mat4_cast(pose->orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{ 1 }, pose->position);
    const glm::mat4 m           = translation * orientation;

    this->Scene_view::set_world_from_control(m);
    this->Scene_view::update_hover_with_raytrace();
    this->Scene_view::update_grid_hover();
}

void Headset_view::render_headset()
{
    ERHE_PROFILE_FUNCTION

    if (m_headset == nullptr)
    {
        return;
    }

    auto frame_timing = m_headset->begin_frame();
    if (!frame_timing.begin_ok)
    {
        return;
    }

    if (frame_timing.should_render)
    {
        auto callback = [this](erhe::xr::Render_view& render_view) -> bool
        {
            const auto& view_resources = get_headset_view_resources(render_view);
            if (!view_resources->is_valid)
            {
                return false;
            }

            if (m_head_tracking_enabled)
            {
                view_resources->update(render_view);
            }

            auto* framebuffer = view_resources->framebuffer.get();
            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, framebuffer->gl_name());

            auto status = gl::check_named_framebuffer_status(framebuffer->gl_name(), gl::Framebuffer_target::draw_framebuffer);
            if (status != gl::Framebuffer_status::framebuffer_complete)
            {
                log_headset->error("view framebuffer status = {}", gl::c_str(status));
            }

            const erhe::scene::Viewport viewport
            {
                .x             = 0,
                .y             = 0,
                .width         = static_cast<int>(render_view.width),
                .height        = static_cast<int>(render_view.height),
                .reverse_depth = erhe::application::g_configuration->graphics.reverse_depth
            };
            gl::viewport(0, 0, render_view.width, render_view.height);

            erhe::graphics::g_opengl_state_tracker->shader_stages.reset();
            erhe::graphics::g_opengl_state_tracker->color_blend.execute(Color_blend_state::color_blend_disabled);
            gl::viewport(
                viewport.x,
                viewport.y,
                viewport.width,
                viewport.height
            );
            gl::enable(gl::Enable_cap::framebuffer_srgb);

            //// auto* squeeze_click = m_headset->get_actions_right().squeeze_click;
            //// if ((squeeze_click != nullptr) && (squeeze_click->state.currentState == XR_TRUE))
            //// {
            ////     ERHE_PROFILE_GPU_SCOPE(c_id_headset_clear)
            ////
            ////     gl::clear_color(0.0f, 0.0f, 0.0f, 0.0f);
            ////     gl::clear(gl::Clear_buffer_mask::color_buffer_bit);
            //// }
            //// else
            {
                const glm::vec4 clear_color{0.0f, 0.0f, 0.0f, 0.0f};

                gl::clear_color(
                    clear_color[0],
                    clear_color[1],
                    clear_color[2],
                    clear_color[3]
                );
                gl::clear_depth_f(*erhe::application::g_configuration->depth_clear_value_pointer());
                gl::clear(
                    gl::Clear_buffer_mask::color_buffer_bit |
                    gl::Clear_buffer_mask::depth_buffer_bit
                );

                Viewport_config viewport_config;

                Render_context render_context {
                    .scene_view      = this,
                    .viewport_config = &viewport_config,
                    .camera          = as_camera(view_resources->camera.get()),
                    .viewport        = viewport
                };

                g_editor_rendering->render_content(render_context, true);
                g_editor_rendering->render_selection(render_context, true);

                if (erhe::application::g_line_renderer_set != nullptr) // && m_headset->trigger_value() > 0.0f)
                {
                    ERHE_PROFILE_GPU_SCOPE(c_id_headset_render_content)
                    erhe::application::g_line_renderer_set->begin();
                    g_tools->render_tools(render_context);
                    erhe::application::g_line_renderer_set->render(viewport, *render_context.camera);
                    erhe::application::g_line_renderer_set->end();
                }
                g_editor_rendering->render_content            (render_context, false);
                g_editor_rendering->render_selection          (render_context, false);
                g_editor_rendering->render_brush              (render_context);
                g_editor_rendering->render_tool_meshes        (render_context);
                g_editor_rendering->render_rendertarget_meshes(render_context);
            }

            return true;
        };
        m_headset->render(callback);
    }

    m_headset->end_frame(frame_timing.should_render);
}

void Headset_view::setup_root_camera()
{
    ERHE_PROFILE_FUNCTION

    m_root_camera = std::make_shared<erhe::scene::Camera>(
        "Headset Root Camera"
    );

    auto& projection = *m_root_camera->projection();
    projection.fov_y           = glm::radians(35.0f);
    projection.projection_type = erhe::scene::Projection::Type::perspective_vertical;
    projection.z_near          = 0.03f;
    projection.z_far           = 200.0f;

    const glm::mat4 m = erhe::toolkit::create_look_at(
        glm::vec3{0.0f, 0.0f, 0.0f}, // eye
        glm::vec3{0.0f, 0.0f, 0.0f}, // look at
        glm::vec3{0.0f, 1.0f, 0.0f}  // up
    );

    m_root_node = m_scene_root->scene().get_root_node();
    auto root_camera_node = std::make_shared<erhe::scene::Node>(
        "Headset Root Camera Node"
    );

    root_camera_node->set_parent_from_node(m);
    root_camera_node->attach(m_root_camera);
    root_camera_node->set_parent(m_root_node);
}

auto Headset_view::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

auto Headset_view::get_root_node() const -> std::shared_ptr<erhe::scene::Node>
{
    return m_root_node;
}

auto Headset_view::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_root_camera;
}

auto Headset_view::get_rendergraph_node() -> std::shared_ptr<erhe::application::Rendergraph_node>
{
    return m_rendergraph_node;
}

auto Headset_view::get_shadow_render_node() const -> Shadow_render_node*
{
    return m_shadow_render_node.get();
}

void Headset_view::add_finger_input(
    const Finger_point& finger_input
)
{
    m_finger_inputs.push_back(finger_input);
}

[[nodiscard]] auto Headset_view::finger_to_viewport_distance_threshold() const -> float
{
    return m_finger_to_viewport_distance_threshold;
}

[[nodiscard]] auto Headset_view::get_headset() const -> erhe::xr::Headset*
{
    return m_headset.get();
}

void Headset_view::begin_frame()
{
    ERHE_PROFILE_FUNCTION

    if (!m_headset)
    {
        return;
    }

    // TODO Consider multiple scene view being able to be (hover) active
    //      (viewport window and headset view).
    g_editor_message_bus->send_message(
        Editor_message{
            .update_flags = Message_flag_bit::c_flag_bit_hover_scene_view | Message_flag_bit::c_flag_bit_render_scene_view,
            .scene_view   = this
        }
    );

    m_finger_inputs.clear();
    update_pointer_context_from_controller();
    auto& commands = *erhe::application::g_commands;
    auto& instance = m_headset->get_xr_instance();
    const XrSession xr_session = m_headset->get_xr_session().get_xr_session();
    for (auto& action : instance.get_boolean_actions())
    {
        action.get(xr_session);
        if (action.state.changedSinceLastSync == XR_TRUE)
        {
            commands.on_xr_action(action);
        }
    }
    for (auto& action : instance.get_float_actions())
    {
        action.get(xr_session);
        if (action.state.changedSinceLastSync == XR_TRUE)
        {
            commands.on_xr_action(action);
        }
    }
    for (auto& action : instance.get_vector2f_actions())
    {
        action.get(xr_session);
        if (action.state.changedSinceLastSync == XR_TRUE)
        {
            commands.on_xr_action(action);
        }
    }

    if (m_controller_visualization)
    {
        // TODO both controllers
        auto* left_aim_pose  = m_headset->get_actions_left().aim_pose;
        auto* right_aim_pose = m_headset->get_actions_right().aim_pose;
        auto* pose =
            (
                (right_aim_pose != nullptr) &&
                (right_aim_pose->location.locationFlags != 0)
            )
                ? right_aim_pose
                : left_aim_pose;
        if (pose != nullptr)
        {
            m_controller_visualization->update(pose);
        }
    }
    //if (m_hand_tracker)
    //{
    //    m_hand_tracker->update_hands(*m_headset.get());
    //}

    const glm::mat4 world_from_view = m_headset->get_view_in_world();
    get_camera()->get_node()->set_world_from_node(world_from_view);

    if (!m_scene_root)
    {
        return;
    }
}

void Headset_view::end_frame()
{
    if (!m_headset)
    {
        return;
    }

    m_finger_inputs.clear();

    // TODO These should be done when session state changes so that polling no longer happens
    auto& instance = m_headset->get_xr_instance();
    for (auto& action : instance.get_boolean_actions())
    {
        action.state.changedSinceLastSync = XR_FALSE;
    }
    for (auto& action : instance.get_float_actions())
    {
        action.state.changedSinceLastSync = XR_FALSE;
    }
    for (auto& action : instance.get_vector2f_actions())
    {
        action.state.changedSinceLastSync = XR_FALSE;
    }
}

void Headset_view::imgui()
{
    m_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);

    ImGui::Checkbox("Head Tracking Enabled", &m_head_tracking_enabled);
}

}