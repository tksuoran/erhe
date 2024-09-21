#include "tools/hud.hpp"

#include "editor_context.hpp"
#include "editor_message_bus.hpp"
#include "editor_windows.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "tools/tools.hpp"
#include "rendertarget_mesh.hpp"
#include "rendertarget_imgui_host.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

namespace editor {

using glm::vec3;

#pragma region Commands
Hud_drag_command::Hud_drag_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Hud.drag"}
    , m_context{context}
{
}

void Hud_drag_command::try_ready()
{
    if (get_command_state() != erhe::commands::State::Inactive) {
        return;
    }

    if (m_context.hud->try_begin_drag()) {
        set_active();
    }
}

auto Hud_drag_command::try_call() -> bool
{
    if (get_command_state() != erhe::commands::State::Active) {
        return false;
    }

    m_context.hud->on_drag();

    return true;
}

void Hud_drag_command::on_inactive()
{
    if (
        (get_command_state() == erhe::commands::State::Ready ) ||
        (get_command_state() == erhe::commands::State::Active)
    ) {
        m_context.hud->end_drag();
    }
}

Toggle_hud_visibility_command::Toggle_hud_visibility_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Hud.toggle_visibility"}
    , m_context{context}
{
}

auto Toggle_hud_visibility_command::try_call() -> bool
{
    m_context.hud->toggle_mesh_visibility();
    return true;
}
#pragma endregion Commands

Hud::Hud(
    erhe::commands::Commands&       commands,
    erhe::graphics::Instance&       graphics_instance,
    erhe::imgui::Imgui_renderer&    imgui_renderer,
    erhe::rendergraph::Rendergraph& rendergraph,
    Editor_context&                 editor_context,
    Editor_message_bus&             editor_message_bus,
    Editor_windows&                 editor_windows,
    Headset_view&                   headset_view,
    Mesh_memory&                    mesh_memory,
    Scene_builder&                  scene_builder,
    Tools&                          tools
)
    : Tool                                {editor_context}
    , m_toggle_visibility_command         {commands, editor_context}
#if defined(ERHE_XR_LIBRARY_OPENXR)
    , m_drag_command                      {commands, editor_context}
    , m_drag_float_redirect_update_command{commands, m_drag_command}
    , m_drag_float_enable_command         {commands, m_drag_float_redirect_update_command, 0.3f, 0.1f}
    , m_drag_bool_redirect_update_command {commands, m_drag_command}
    , m_drag_bool_enable_command          {commands, m_drag_bool_redirect_update_command}
#endif
{
    ERHE_PROFILE_FUNCTION();

    int   width {1024};
    int   height{1024};
    float ppm   {5000.0f};
    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "hud");
    ini.get("enabled", m_enabled);
    ini.get("width",   width);
    ini.get("height",  height);
    ini.get("ppm",     ppm);
    ini.get("x",       m_x);
    ini.get("y",       m_y);
    ini.get("z",       m_z);

    if (!m_enabled) {
        return;
    }

    set_description("Hud");
    set_flags      (Tool_flags::background);
    tools.register_tool(this);

    commands.register_command   (&m_toggle_visibility_command);
    commands.bind_command_to_key(&m_toggle_visibility_command, erhe::window::Key_e, true);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    const auto* headset = headset_view.get_headset();
    if (headset != nullptr) {
        auto& xr_right = headset->get_actions_right();
        commands.bind_command_to_xr_boolean_action(&m_toggle_visibility_command, xr_right.menu_click, erhe::commands::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_boolean_action(&m_toggle_visibility_command, xr_right.b_click,    erhe::commands::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_float_action  (&m_drag_float_enable_command, xr_right.squeeze_value);
        commands.bind_command_to_update           (&m_drag_float_redirect_update_command);
        commands.bind_command_to_xr_boolean_action(&m_drag_bool_enable_command, xr_right.squeeze_click, erhe::commands::Button_trigger::Any);
        commands.bind_command_to_update           (&m_drag_bool_redirect_update_command);
    }
    m_drag_command.set_host(this);
#else
    static_cast<void>(headset_view);
#endif

    m_rendertarget_mesh = std::make_shared<Rendertarget_mesh>(graphics_instance, mesh_memory, width, height, ppm);
    auto scene_root = scene_builder.get_scene_root();
    m_rendertarget_mesh->layer_id = scene_root->layers().rendertarget()->id;
    m_rendertarget_mesh->enable_flag_bits(
        erhe::Item_flags::rendertarget |
        erhe::Item_flags::visible      |
        erhe::Item_flags::translucent  |
        (editor_context.developer_mode ? erhe::Item_flags::show_in_ui : 0)
    );

    m_rendertarget_node = std::make_shared<erhe::scene::Node>("Hud RT node");
    m_rendertarget_node->set_parent(scene_root->get_scene().get_root_node());
    m_rendertarget_node->attach(m_rendertarget_mesh);
    m_rendertarget_node->enable_flag_bits(
        erhe::Item_flags::rendertarget |
        erhe::Item_flags::visible      |
        (editor_context.developer_mode ? erhe::Item_flags::show_in_ui : 0)
    );

    m_rendertarget_imgui_viewport = std::make_shared<editor::Rendertarget_imgui_host>(
        imgui_renderer,
        rendergraph,
        editor_context,
        m_rendertarget_mesh.get(),
        "Hud Viewport",
        true
    );

    m_rendertarget_imgui_viewport->set_begin_callback(
        [&editor_windows](erhe::imgui::Imgui_host& imgui_host) {
            editor_windows.viewport_menu(imgui_host);
        }
    );

    set_mesh_visibility(true);

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );
}

auto Hud::get_rendertarget_imgui_viewport() const -> std::shared_ptr<Rendertarget_imgui_host>
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

auto Hud::intersect_ray(const glm::vec3& ray_origin_in_world, const glm::vec3& ray_direction_in_world) -> std::optional<glm::vec3>
{
    const glm::vec3 ray_origin_in_hud    = glm::vec3{node_from_world() * glm::vec4{ray_origin_in_world,    1.0f}};
    const glm::vec3 ray_direction_in_hud = glm::vec3{node_from_world() * glm::vec4{ray_direction_in_world, 0.0f}};
    const auto intersection = erhe::math::intersect_plane<float>(
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 0.0f},
        ray_origin_in_hud,
        ray_direction_in_hud
    );
    if (!intersection.has_value()) {
        return {};
    }
    const glm::vec3 position_in_node = ray_origin_in_hud + intersection.value() * ray_direction_in_hud;

    const auto half_width  = 0.5f * m_rendertarget_mesh->width();
    const auto half_height = 0.5f * m_rendertarget_mesh->height();

    if (
        (position_in_node.x < -half_width ) ||
        (position_in_node.x >  half_width ) ||
        (position_in_node.z < -half_height) ||
        (position_in_node.z >  half_height)
    ) {
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
    if (scene_view == nullptr) {
        return false;
    }

    const auto world_from_control_opt = scene_view->get_world_from_control();
    if (!world_from_control_opt.has_value()) {
        return false;
    }

    const auto* drag_entry = scene_view->get_nearest_hover(
        Hover_entry::rendertarget_bit
    );
    if ((drag_entry == nullptr) || !drag_entry->valid || (drag_entry->mesh == nullptr)) {
        return false;
    }
    auto* node = drag_entry->mesh->get_node();
    if (node == nullptr) {
        return false;
    }
    m_drag_node = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());
    auto drag_node = m_drag_node.lock();
    if (!drag_node) {
        return false;
    }
    const glm::mat4 world_from_control = world_from_control_opt.value();
    const glm::mat4 node_from_world    = drag_node->node_from_world();
    const glm::mat4 world_from_node    = drag_node->world_from_node();
    m_node_from_control = node_from_world * world_from_control;
    return true;
}

void Hud::on_drag()
{
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }

    const auto control_from_world_opt = scene_view->get_control_from_world();
    if (!control_from_world_opt.has_value() || !m_node_from_control.has_value()) {
        return;
    }

    auto drag_node = m_drag_node.lock();
    if (!drag_node) {
        return;
    }

    const glm::mat4 control_from_world = control_from_world_opt.value();
    const glm::mat4 node_from_world    = m_node_from_control.value() * control_from_world;
    drag_node->set_node_from_world(node_from_world);
}

void Hud::end_drag()
{
    m_node_from_control.reset();
    m_drag_node.reset();
}

void Hud::on_message(Editor_message& message)
{
    Tool::on_message(message);
}

void Hud::update_node_transform(const glm::mat4& world_from_hud)
{
    if (!m_rendertarget_node) {
        return;
    }

    m_world_from_hud = world_from_hud;
    const glm::mat4 offset = erhe::math::create_translation(m_x, m_y, m_z);
    const glm::mat4 final_transform = m_world_from_hud * offset;
    m_rendertarget_node->set_world_from_node(final_transform);
}

void Hud::tool_render(const Render_context&)
{
}

void Hud::imgui()
{
    ImGui::Checkbox("Mesh visible", &m_mesh_visible);

    float rendertarget_mesh_lod_bias = Rendertarget_mesh::get_mesh_lod_bias();
    ImGui::SliderFloat("Hud LoD Bias", &rendertarget_mesh_lod_bias, -8.0f, 8.0f);
    if (ImGui::IsItemEdited()) {
        Rendertarget_mesh::set_mesh_lod_bias(rendertarget_mesh_lod_bias);
    }

    const bool x_changed = ImGui::DragFloat("X", &m_x, 0.0001f);
    const bool y_changed = ImGui::DragFloat("Y", &m_y, 0.0001f);
    const bool z_changed = ImGui::DragFloat("Z", &m_z, 0.0001f);
    if (x_changed || y_changed || z_changed) {
        update_node_transform(m_world_from_hud);
    }
}

auto Hud::toggle_mesh_visibility() -> bool
{
    if (!m_enabled) {
        return false;
    }

    set_mesh_visibility(!m_mesh_visible);
    return m_mesh_visible;
}

void Hud::set_mesh_visibility(const bool value)
{
    if (!m_enabled || !m_rendertarget_mesh) {
        return;
    }

    m_mesh_visible = value;

    Scene_view* hover_scene_view = get_hover_scene_view();
    if (hover_scene_view != nullptr) {
        const auto& world_from_control_opt = hover_scene_view->get_world_from_control();
        if (world_from_control_opt.has_value()) {
            update_node_transform(world_from_control_opt.value());
        }
    }

    m_rendertarget_imgui_viewport->set_enabled(m_mesh_visible);
    m_rendertarget_node->set_visible(m_mesh_visible);
}

} // namespace editor
