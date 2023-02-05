#include "tools/hud.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tools.hpp"
#include "rendertarget_mesh.hpp"
#include "rendertarget_imgui_viewport.hpp"

#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/raytrace/iinstance.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/raytrace/ray.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe/xr/xr_action.hpp"
#   include "erhe/xr/headset.hpp"
#endif

namespace editor
{

using glm::vec3;

#pragma region Commands
Hud_drag_command::Hud_drag_command()
    : Command{"Hud.drag"}
{
}

void Hud_drag_command::try_ready()
{
    if (get_command_state() != erhe::application::State::Inactive)
    {
        return;
    }

    if (g_hud->try_begin_drag())
    {
        set_active();
    }
}

auto Hud_drag_command::try_call() -> bool
{
    if (get_command_state() != erhe::application::State::Active)
    {
        return false;
    }

    g_hud->on_drag();

    return true;
}

void Hud_drag_command::on_inactive()
{
    if (
        (get_command_state() == erhe::application::State::Ready ) ||
        (get_command_state() == erhe::application::State::Active)
    )
    {
        g_hud->end_drag();
    }
}

Toggle_hud_visibility_command::Toggle_hud_visibility_command()
    : Command{"Hud.toggle_visibility"}
{
}

auto Toggle_hud_visibility_command::try_call() -> bool
{
    g_hud->toggle_visibility();
    return true;
}
#pragma endregion Commands

Hud* g_hud{nullptr};

Hud::Hud()
    : erhe::components::Component         {c_type_name}
    , erhe::application::Imgui_window     {c_title}
#if defined(ERHE_XR_LIBRARY_OPENXR)
    , m_drag_float_redirect_update_command{m_drag_command}
    , m_drag_float_enable_command         {m_drag_float_redirect_update_command, 0.3f, 0.1f}
    , m_drag_bool_redirect_update_command {m_drag_command}
    , m_drag_bool_enable_command          {m_drag_bool_redirect_update_command}
#endif
{
}

Hud::~Hud() noexcept
{
    ERHE_VERIFY(g_hud == nullptr);
}

void Hud::deinitialize_component()
{
    ERHE_VERIFY(g_hud == this);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    m_drag_command.set_host(nullptr);
#endif
    m_rendertarget_node.reset();
    m_rendertarget_mesh.reset();
    m_rendertarget_imgui_viewport.reset();
    g_hud = nullptr;
}

void Hud::declare_required_components()
{
    require<erhe::application::Commands           >();
    require<erhe::application::Configuration      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows      >();
    require<erhe::application::Rendergraph        >();
    require<Editor_message_bus>();
    require<Scene_builder     >();
    require<Tools             >();
    require<Viewport_windows  >();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    require<Headset_view      >();
#endif
}

void Hud::initialize_component()
{
    ERHE_PROFILE_FUNCTION
    ERHE_VERIFY(g_hud == nullptr);
    g_hud = this; // due to early out

    const auto& configuration = *erhe::application::g_configuration;
    const auto& config        = configuration.hud;

    m_enabled    = config.enabled;
    m_is_visible = config.show;
    m_x          = config.x;
    m_y          = config.y;
    m_z          = config.z;

    if (!m_enabled)
    {
        return;
    }

    erhe::application::g_imgui_windows->register_imgui_window(this);

    const erhe::application::Scoped_gl_context gl_context;

    set_description(c_title);
    set_flags      (Tool_flags::background);
    g_tools->register_tool(this);

    auto& commands = *erhe::application::g_commands;
    commands.register_command   (&m_toggle_visibility_command);
    commands.bind_command_to_key(&m_toggle_visibility_command, erhe::toolkit::Key_e, true);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    const auto* headset = g_headset_view->get_headset();
    if (headset != nullptr)
    {
        auto& xr_right = headset->get_actions_right();
        commands.bind_command_to_xr_boolean_action(&m_toggle_visibility_command, xr_right.menu_click, erhe::application::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_boolean_action(&m_toggle_visibility_command, xr_right.b_click,    erhe::application::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_float_action  (&m_drag_float_enable_command, xr_right.squeeze_value);
        commands.bind_command_to_update           (&m_drag_float_redirect_update_command);
        commands.bind_command_to_xr_boolean_action(&m_drag_bool_enable_command, xr_right.squeeze_click, erhe::application::Button_trigger::Any);
        commands.bind_command_to_update           (&m_drag_bool_redirect_update_command);
    }
    m_drag_command.set_host(this);
#endif

    m_rendertarget_mesh = std::make_shared<Rendertarget_mesh>(
        config.width,
        config.height,
        config.ppm
    );
    const auto& scene_root = g_scene_builder->get_scene_root();
    m_rendertarget_mesh->mesh_data.layer_id = scene_root->layers().rendertarget()->id;
    m_rendertarget_mesh->enable_flag_bits(
        erhe::scene::Item_flags::content |
        erhe::scene::Item_flags::visible |
        erhe::scene::Item_flags::show_in_ui
    );

    //m_rendertarget_mesh->disable_flag_bits(erhe::scene::Item_flags::visible);

    m_rendertarget_node = std::make_shared<erhe::scene::Node>("Hud RT node");
    m_rendertarget_node->set_parent(scene_root->scene().get_root_node());
    m_rendertarget_node->attach(m_rendertarget_mesh);
    m_rendertarget_node->enable_flag_bits(
        erhe::scene::Item_flags::content |
        erhe::scene::Item_flags::visible |
        erhe::scene::Item_flags::show_in_ui
    );
    auto node_raytrace = m_rendertarget_mesh->get_node_raytrace();
    if (node_raytrace)
    {
        m_rendertarget_node->attach(node_raytrace);
    }

    m_rendertarget_imgui_viewport = std::make_shared<editor::Rendertarget_imgui_viewport>(
        m_rendertarget_mesh.get(),
        "Hud Viewport"
    );

    m_rendertarget_imgui_viewport->set_menu_visible(true);
    erhe::application::g_imgui_windows->register_imgui_viewport(m_rendertarget_imgui_viewport);

    set_visibility(m_is_visible);

    g_editor_message_bus->add_receiver(
        [&](Editor_message& message)
        {
            on_message(message);
        }
    );
}

[[nodiscard]] auto Hud::get_rendertarget_imgui_viewport() const -> std::shared_ptr<Rendertarget_imgui_viewport>
{
    return m_rendertarget_imgui_viewport;
}

auto Hud::world_from_node() const -> glm::mat4
{
    ERHE_VERIFY(m_rendertarget_node);
    return m_rendertarget_node->world_from_node();
}

auto Hud::node_from_world() const -> glm::mat4
{
    ERHE_VERIFY(m_rendertarget_node);
    return m_rendertarget_node->node_from_world();
}

auto Hud::intersect_ray(
    const glm::vec3& ray_origin_in_world,
    const glm::vec3& ray_direction_in_world
) -> std::optional<glm::vec3>
{
    const glm::vec3 ray_origin_in_grid    = glm::vec3{node_from_world() * glm::vec4{ray_origin_in_world,    1.0f}};
    const glm::vec3 ray_direction_in_grid = glm::vec3{node_from_world() * glm::vec4{ray_direction_in_world, 0.0f}};
    const auto intersection = erhe::toolkit::intersect_plane<float>(
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 0.0f},
        ray_origin_in_grid,
        ray_direction_in_grid
    );
    if (!intersection.has_value())
    {
        return {};
    }
    const glm::vec3 position_in_node = ray_origin_in_grid + intersection.value() * ray_direction_in_grid;

    const auto half_width  = 0.5f * m_rendertarget_mesh->width();
    const auto half_height = 0.5f * m_rendertarget_mesh->height();

    if (
        (position_in_node.x < -half_width ) ||
        (position_in_node.x >  half_width ) ||
        (position_in_node.z < -half_height) ||
        (position_in_node.z >  half_height)
    )
    {
        return {};
    }

    return glm::vec3{
        world_from_node() * glm::vec4{position_in_node, 1.0f}
    };
}

auto Hud::try_begin_drag() -> bool
{
    m_node_from_control.reset();

    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr)
    {
        return false;
    }

    const auto world_from_control_opt = scene_view->get_world_from_control();
    if (!world_from_control_opt.has_value())
    {
        return false;
    }
    const glm::mat4 world_from_control = world_from_control_opt.value();
    const glm::mat4 node_from_world    = m_rendertarget_node->node_from_world();
    const glm::mat4 world_from_node    = m_rendertarget_node->world_from_node();
    m_node_from_control = node_from_world * world_from_control;
    return true;
}

void Hud::on_drag()
{
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr)
    {
        return;
    }

    const auto control_from_world_opt = scene_view->get_control_from_world();
    if (!control_from_world_opt.has_value() || !m_node_from_control.has_value())
    {
        return;
    }
    const glm::mat4 control_from_world = control_from_world_opt.value();
    const glm::mat4 node_from_world    = m_node_from_control.value() * control_from_world;
    m_rendertarget_node->set_node_from_world(node_from_world);
}

void Hud::end_drag()
{
    m_node_from_control.reset();
}

void Hud::on_message(Editor_message& message)
{
    Tool::on_message(message);

    if (m_locked_to_head)
    {
        using namespace erhe::toolkit;
        if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_render_scene_view))
        {
            const auto& camera = message.scene_view->get_camera();
            if (camera)
            {
                const auto* camera_node = camera->get_node();
                if (camera_node != nullptr)
                {
                    const auto& world_from_camera = camera_node->world_from_node();
                    update_node_transform(world_from_camera);
                }
            }
        }
    }
}

void Hud::update_node_transform(const glm::mat4& world_from_camera)
{
    if (!m_rendertarget_node)
    {
        return;
    }

    m_world_from_camera = world_from_camera;
    const glm::vec3 target_position{world_from_camera * glm::vec4{0.0, m_y, 0.0, 1.0}};
    const glm::vec3 eye_position{world_from_camera * glm::vec4{m_x, m_y, m_z, 1.0}};
    const glm::vec3 up_direction{world_from_camera * glm::vec4{0.0, 1.0, 0.0, 0.0}};

    const glm::mat4 m = erhe::toolkit::create_look_at(
        eye_position,
        target_position,
        up_direction
    );

    m_rendertarget_node->set_world_from_node(m);
}

void Hud::tool_render(
    const Render_context& /*context*/
)
{
}

void Hud::imgui()
{
    ImGui::Checkbox("Locked to Head", &m_locked_to_head);
    ImGui::Checkbox("Visible",        &m_is_visible);
    const bool x_changed = ImGui::DragFloat("X", &m_x, 0.0001f);
    const bool y_changed = ImGui::DragFloat("Y", &m_y, 0.0001f);
    const bool z_changed = ImGui::DragFloat("Z", &m_z, 0.0001f);
    if (x_changed || y_changed || z_changed)
    {
        update_node_transform(m_world_from_camera);
    }
}

auto Hud::toggle_visibility() -> bool
{
    if (!m_enabled)
    {
        return false;
    }

    set_visibility(!m_is_visible);
    return m_is_visible;
}

void Hud::set_visibility(const bool value)
{
    if (!m_enabled)
    {
        return;
    }

    m_is_visible = value;

    if (!m_rendertarget_mesh)
    {
        return;
    }

    Scene_view* hover_scene_view = get_hover_scene_view();
    if (hover_scene_view != nullptr)
    {
        const auto& camera = get_hover_scene_view()->get_camera();
        if (camera)
        {
            const auto* camera_node = camera->get_node();
            if (camera_node != nullptr)
            {
                const auto& world_from_camera = camera_node->world_from_node();
                update_node_transform(world_from_camera);
            }
        }
    }

    m_rendertarget_imgui_viewport->set_enabled(m_is_visible);
    m_rendertarget_node->set_visible(m_is_visible);
}

} // namespace editor
