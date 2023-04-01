#include "tools/hotbar.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "graphics/icon_set.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/content_library.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"
#include "rendertarget_mesh.hpp"
#include "rendertarget_imgui_viewport.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif
#include "windows/operations.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/geometry/shapes/disc.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe/xr/xr_action.hpp"
#   include "erhe/xr/headset.hpp"
#endif

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

// https://math.stackexchange.com/questions/1662616/calculate-the-diameter-of-an-inscribed-circle-inside-a-sector-of-circle
//
//                                                                               .
//    |---___                                          r = (R-r) * sin(theta/2)  .
//   |    ___---___                                                              .
//  |   _/         ---___                                                        .
//  | _/              \_ ---___                            R * sin(theta/2)      .
// | /                  \      ---___                  r = ----------------      .
// ||                    |           ---___                1 + sin(theta/2)      .
// ||   r                |    R-r          ---___                                .
// +----------X----------+--------------------------                             .
// ||          \         |                 ___---                                .
// ||           \        |           ___---                                      .
// | \           \r     /      ___---                                            .
//  | \_          \    / ___---                                                  .
//  |   \_         \__---                                                        .
//   |    \_____---                                                              .
//    |___---                                                                    .
//                                                                               .

namespace editor
{

using glm::vec3;

#pragma region Commmands
Toggle_menu_visibility_command::Toggle_menu_visibility_command()
    : Command{"Hotbar.toggle_visibility"}
{
}

auto Toggle_menu_visibility_command::try_call() -> bool
{
    g_hotbar->toggle_visibility();
    return true;
}

Hotbar_trackpad_command::Hotbar_trackpad_command()
    : Command{"Hotbar.trackpad"}
{
}

auto Hotbar_trackpad_command::try_call_with_input(
    erhe::application::Input_arguments& input
) -> bool
{
    return g_hotbar->try_call(input);
}
#pragma endregion Commands

Hotbar* g_hotbar{nullptr};

Hotbar::Hotbar()
    : erhe::application::Imgui_window{c_title}
    , erhe::components::Component    {c_type_name}
#if defined(ERHE_XR_LIBRARY_OPENXR)
    , m_trackpad_command             {}
    , m_trackpad_click_command       {m_trackpad_command}
#endif
{
}

Hotbar::~Hotbar() noexcept
{
    ERHE_VERIFY(g_hotbar == nullptr);
}

void Hotbar::deinitialize_component()
{
    ERHE_VERIFY(g_hotbar == this);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    m_trackpad_click_command.set_host(nullptr);
    m_trackpad_command.set_host(nullptr);
#endif

    m_rendertarget_mesh.reset();

    m_rendertarget_node.reset();

    m_rendertarget_imgui_viewport.reset();
    m_slots.clear();
    g_hotbar = nullptr;
}

void Hotbar::declare_required_components()
{
    require<erhe::application::Commands           >();
    require<erhe::application::Configuration      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows      >();
    require<erhe::application::Rendergraph        >();
    require<Editor_message_bus>();
    require<Scene_builder     >();
    require<Viewport_windows  >();
    require<Icon_set          >();
    require<Tools             >();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    require<Headset_view      >();
#endif
}

void Hotbar::initialize_component()
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(g_hotbar == nullptr);

    auto ini = erhe::application::get_ini("erhe.ini", "hotbar");
    ini->get("enabled",    m_enabled);
    ini->get("show",       m_show);
    ini->get("use_radial", m_use_radial);
    ini->get("x",          m_x);
    ini->get("y",          m_y);
    ini->get("z",          m_z);

    if (!m_enabled) {
        return;
    }

    auto& commands = *erhe::application::g_commands;
    commands.register_command   (&m_toggle_visibility_command);
    commands.bind_command_to_key(&m_toggle_visibility_command, erhe::toolkit::Key_space, true);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    commands.register_command(&m_trackpad_command);
    const auto* headset = g_headset_view->get_headset();
    if (headset != nullptr)
    {
        auto& xr_right = headset->get_actions_right();
        m_trackpad_click_command.bind(xr_right.trackpad);
        commands.bind_command_to_xr_boolean_action(&m_trackpad_click_command, xr_right.trackpad_click, erhe::application::Button_trigger::Button_pressed);
        m_trackpad_click_command.set_host(this);
        m_trackpad_command.set_host(this);
    }
#endif

    const erhe::application::Scoped_gl_context gl_context;

    set_description(c_title);
    set_flags      (Tool_flags::background);
    g_tools->register_tool(this);

    set_visibility(m_show);

    if (m_use_radial) {
        init_radial_menu();
    } else {
        erhe::application::g_imgui_windows->register_imgui_window(this, nullptr);
        this->Imgui_window::m_show_in_menu = false;
        init_hotbar();
    }

    g_editor_message_bus->add_receiver(
        [&](Editor_message& message)
        {
            on_message(message);
        }
    );

    g_hotbar = this;
}

void Hotbar::init_hotbar()
{
    const auto& scene_root         = g_scene_builder->get_scene_root();
    const auto& icon_rasterization = g_icon_set->get_hotbar_rasterization();
    const int   icon_size          = icon_rasterization.get_size();

    m_rendertarget_mesh = std::make_shared<Rendertarget_mesh>(
        icon_size * 8,
        icon_size,
        4000.0f
    );
    m_rendertarget_mesh->mesh_data.layer_id = scene_root->layers().rendertarget()->id;

    m_rendertarget_mesh->enable_flag_bits(
        erhe::scene::Item_flags::visible     |
        erhe::scene::Item_flags::translucent
    );

    m_rendertarget_imgui_viewport = std::make_shared<editor::Rendertarget_imgui_viewport>(
        m_rendertarget_mesh.get(),
        "Hotbar Viewport",
        false
    );

    m_rendertarget_node = std::make_shared<erhe::scene::Node>("Hotbar RT node");
    m_rendertarget_node->attach(m_rendertarget_mesh);
    auto node_raytrace = m_rendertarget_mesh->get_node_raytrace();
    if (node_raytrace) {
        m_rendertarget_node->attach(node_raytrace);
        m_rendertarget_node->show();
    }

    m_rendertarget_imgui_viewport->set_clear_color(glm::vec4{0.0f, 0.0f, 0.0f, 0.0f});

    // Also registers rendertarget node
    erhe::application::g_imgui_windows->register_imgui_viewport(m_rendertarget_imgui_viewport);

    this->set_viewport(m_rendertarget_imgui_viewport.get());
}

void Hotbar::init_radial_menu()
{
    const float outer_radius = 1.0f;
    const float inner_radius = 0.125f;
    const int   slice_count  = 100;
    const int   stack_count  = 2;

    auto disc_material = std::make_shared<erhe::primitive::Material>(
        "Circular Menu Disc",
        glm::vec4{0.1f, 0.2f, 0.3f, 1.0f}
    );
    disc_material->opacity = 0.5f;

    const auto disc_geometry_shared = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::shapes::make_disc(
            outer_radius,
            inner_radius,
            slice_count,
            stack_count,
            20,
            30,
            0,
            2
        )
    );

    auto primitive_geometry = erhe::primitive::make_primitive(*disc_geometry_shared.get(), g_mesh_memory->build_info);
    auto raytrace_primitive = std::make_shared<Raytrace_primitive>(disc_geometry_shared);

    m_radial_menu_background_mesh = std::make_shared<erhe::scene::Mesh>(
        "Radiaul Menu Mesh",
        erhe::primitive::Primitive{
            .material              = disc_material,
            .gl_primitive_geometry = primitive_geometry,
            .rt_primitive_geometry = raytrace_primitive->primitive_geometry,
            .source_geometry       = disc_geometry_shared
        }
    );

    const auto& scene_root = g_scene_builder->get_scene_root();
    auto*       scene      = scene_root->get_hosted_scene();
    const auto  root_node  = scene->get_root_node();
    ERHE_VERIFY(scene != nullptr);
    ERHE_VERIFY(root_node);
    m_radial_menu_background_mesh->mesh_data.layer_id = scene_root->layers().content()->id;

    m_radial_menu_background_mesh->enable_flag_bits(
        erhe::scene::Item_flags::content     |
        erhe::scene::Item_flags::visible     |
        erhe::scene::Item_flags::translucent |
        erhe::scene::Item_flags::show_in_ui
    );

    m_radial_menu_node = std::make_shared<erhe::scene::Node>("Radial menu node");
    m_radial_menu_node->attach(m_radial_menu_background_mesh);
    m_radial_menu_node->enable_flag_bits(
        erhe::scene::Item_flags::content    |
        erhe::scene::Item_flags::visible    |
        erhe::scene::Item_flags::show_in_ui
    );

    // TODO auto node_raytrace = m_radial_menu_background_mesh->get_node_raytrace();
    // if (node_raytrace) {
    //     m_rendertarget_node->attach(node_raytracm_radial_menu_background_mesh);
    //     m_rendertarget_node->show();
    // }
}

void Hotbar::post_initialize()
{
    m_slot_first = 0;
    m_slot_last  = 0;
    const auto& tools = g_tools->get_tools();
    for (Tool* tool : tools) {
        const auto opt_icon = tool->get_icon();
        if (!opt_icon.has_value()) {
            continue;
        }
        if (erhe::toolkit::test_all_rhs_bits_set(tool->get_flags(), Tool_flags::toolbox)) {
            m_slot_last = m_slots.size();
            m_slots.push_back(tool);
        }
    }
}

void Hotbar::on_message(Editor_message& message)
{
    Scene_view* const old_scene_view = get_hover_scene_view();

    Tool::on_message(message);

    if (!m_enabled || !m_show) {
        return;
    }

    using namespace erhe::toolkit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
        if (message.scene_view != old_scene_view) {
            if (m_use_radial) {
                update_node_transform();
            } else {
                using Rendergraph_node = erhe::application::Rendergraph_node;
                auto old_node = (old_scene_view     != nullptr) ? old_scene_view    ->get_rendergraph_node() : std::shared_ptr<Rendergraph_node>{};
                auto new_node = (message.scene_view != nullptr) ? message.scene_view->get_rendergraph_node() : std::shared_ptr<Rendergraph_node>{};
                if (old_node != new_node) {
                    if (old_node) {
                        erhe::application::g_rendergraph->disconnect(
                            erhe::application::Rendergraph_node_key::rendertarget_texture,
                            m_rendertarget_imgui_viewport,
                            old_node
                        );
                    }
                    set_visibility(static_cast<bool>(new_node));
                    if (new_node) {
                        erhe::application::g_rendergraph->connect(
                            erhe::application::Rendergraph_node_key::rendertarget_texture,
                            m_rendertarget_imgui_viewport,
                            new_node
                        );
                    }
                }
            }
        }
    }

    // Update rendertarget node transform to match render camera.
    // This is used only for horizontal hotbar, not for radial menu.
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_render_scene_view)) {
        if (!m_use_radial && (get_hover_scene_view() == message.scene_view)) {
            update_node_transform();
        }
    }
}

auto Hotbar::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    {
        const auto& headset_view = g_headset_view;
        if (headset_view) {
            return headset_view->get_camera();
        }
    }
#endif
    const auto viewport_window = g_viewport_windows->hover_window();
    if (!viewport_window) {
        return {};
    }
    return viewport_window->get_camera();
}

void Hotbar::update_node_transform()
{
    if (m_locked) {
        return;
    }

    const erhe::scene::Node*           camera_node{nullptr};
    std::shared_ptr<erhe::scene::Node> root_node;
    glm::mat4                          world_from_node{1.0f};
    [this, &camera_node, &root_node, &world_from_node]() {
        const Scene_view* scene_view = get_hover_scene_view();
        if (scene_view == nullptr) {
            return;
        }
        const auto& camera = scene_view->get_camera();
        if (!camera) {
            return;
        }
        camera_node = camera->get_node();
        if (camera_node == nullptr) {
            return;
        }

        const auto& world_from_camera = camera_node->world_from_node();
        world_from_node = erhe::toolkit::create_look_at(
            glm::vec3{world_from_camera * glm::vec4{m_x, m_y, m_z, 1.0}}, // eye
            glm::vec3{world_from_camera * glm::vec4{0.0, 0.0, 0.0, 1.0}}, // target
            glm::vec3{world_from_camera * glm::vec4{0.0, 1.0, 0.0, 0.0}}  // up
        );

        auto scene_root = scene_view->get_scene_root();
        if (!scene_root) {
            return;
        }
        auto* scene = scene_root->get_hosted_scene();
        if (scene == nullptr) {
            return;
        }
        root_node = scene->get_root_node();
    }();

    if (m_rendertarget_node) {
        m_rendertarget_node->set_parent         (root_node);
        m_rendertarget_node->set_world_from_node(world_from_node);
    }

    if (m_radial_menu_node) {
        if (root_node) {
            m_radial_menu_node->set_parent         (root_node);
        }
        m_radial_menu_node->set_world_from_node(world_from_node);
    }
}

void Hotbar::tool_render(
    const Render_context& /*context*/
)
{
}

auto Hotbar::flags() -> ImGuiWindowFlags
{
    return
        ImGuiWindowFlags_NoBackground      |
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoNavInputs       |
        ImGuiWindowFlags_NoNavFocus;
}

void Hotbar::on_begin()
{
    if (m_rendertarget_mesh) {
        m_min_size[0] = static_cast<float>(m_rendertarget_mesh->width());
        m_min_size[1] = static_cast<float>(m_rendertarget_mesh->height());
        m_max_size[0] = m_min_size[0];
        m_max_size[1] = m_min_size[1];
    }
    ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
}

void Hotbar::handle_slot_update()
{
    if ((m_slot >= m_slot_first) && (m_slot <= m_slot_last)) {
        Tool* new_tool = m_slots.at(m_slot);
        g_tools->set_priority_tool(new_tool);
    }
}

auto Hotbar::try_call(erhe::application::Input_arguments& input) -> bool
{
    if (!m_enabled) {
        return false;
    }

    const auto position = input.vector2.absolute_value;

    const auto old_slot = m_slot;
    if (position.x < -0.2f) {
        m_slot = (m_slot == m_slot_first)
            ? m_slot_last
            : m_slot - 1;
    }

    if (position.x > 0.2f) {
        m_slot = (m_slot == m_slot_last)
            ? m_slot_first
            : m_slot + 1;
    }

    if (m_slot != old_slot) {
        handle_slot_update();
    }

    return true;
}

auto Hotbar::get_color(const int color) -> glm::vec4&
{
    switch (color) {
        case 0:  return m_color_inactive;
        case 1:  return m_color_hover;
        case 2:  return m_color_active;
        default: return m_color_inactive;
    }
}

auto Hotbar::get_position() const -> glm::vec3
{
    return glm::vec3{m_x, m_y, m_z};
}

void Hotbar::set_position(glm::vec3 position)
{
    m_x = position.x;
    m_y = position.y;
    m_z = position.z;
}

auto Hotbar::get_locked() const -> bool
{
    return m_locked;
}
void Hotbar::set_locked(bool value)
{
    m_locked = value;
}

void Hotbar::tool_button(const uint32_t id, Tool* tool)
{
    const glm::vec4 background_color  {0.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec4 tint_color        {1.0f, 1.0f, 1.0f, 1.0f};
    const auto&     icon_rasterization{g_icon_set->get_hotbar_rasterization()};
    const bool      is_boosted        {tool->get_priority_boost() > 0};
    const auto      opt_icon          {tool->get_icon()};

    if (!opt_icon) {
        return;
    }

    if (is_boosted) {
        ImGui::PushStyleColor(ImGuiCol_Button, m_color_active);
    }

    const bool is_pressed = icon_rasterization.icon_button(
        id,
        opt_icon.value(),
        background_color,
        tint_color
    );

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tool->get_description());
    }

    if (is_boosted) {
        ImGui::PopStyleColor();
    }

    if (is_pressed) {
        g_tools->set_priority_tool(is_boosted ? nullptr : tool);
    }
}

void Hotbar::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PushID("Hotbar::imgui");
    //const ImVec2 button_size{64.0f, 64.0f};
    //ImGui::SliderFloat("X", &m_x, -2.0f, 2.0f);
    //ImGui::SliderFloat("Y", &m_y, -0.3f, 0.0f);
    //ImGui::SliderFloat("Z", &m_z, -2.0f, 2.0f);

    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  m_color_active);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_color_hover);
    ImGui::PushStyleColor(ImGuiCol_Button,        m_color_inactive);

    uint32_t id = 0;
    for (auto* tool : m_slots) {
        tool_button(++id, tool);
    }
    ImGui::PopStyleColor(3);
    ImGui::PopID();
#endif
}

auto Hotbar::toggle_visibility() -> bool
{
    if (!m_enabled) {
        return false;
    }

    update_node_transform();

    set_visibility(!m_is_visible);
    return m_is_visible;
}

void Hotbar::set_visibility(const bool value)
{
    Imgui_window::set_visibility(value);

    if (m_rendertarget_mesh) {
        m_rendertarget_imgui_viewport->set_enabled(value);
        m_rendertarget_mesh->set_visible(value);
        log_hud->info("horizontal menu visibility set to {}", value);
    }

    if (m_radial_menu_background_mesh) {
        m_radial_menu_background_mesh->set_visible(value);
        log_hud->info("radial menu visibility set to {}", value);
    }

    const Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }
    auto scene_root = scene_view->get_scene_root();
    if (!scene_root) {
        return;
    }
    auto* scene = scene_root->get_hosted_scene();
    const auto root_node = scene->get_root_node();
    if (root_node) {
        log_hud->info("root node trace:");
        root_node->trace();
    }
}

} // namespace editor
