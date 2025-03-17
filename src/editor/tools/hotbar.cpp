#include "tools/hotbar.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "graphics/icon_set.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"
#include "rendertarget_mesh.hpp"
#include "rendertarget_imgui_host.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif

#include "erhe_commands/commands.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_geometry/shapes/disc.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
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

// https://math.stackexchange.com/questions/1662616/calculate-the-diameter-of-an-inscribed-circle-inside-a-sector-of-circle
//                                                                              .
// Diameter of an inscribed circle inside a sector of circle:                   .
//                                                                              .
//   r = (R-r) * sin(theta/2)                                                   .
//                                                                              .
//                                                                              .
//       R * sin(theta/2)                                                       .
//   r = ----------------                                                       .
//       1 + sin(theta/2)                                                       .
//                                                                              .
//                                                                              .
//      :..                                                                     .
//     :   ˙˙··..                                                               .
//     :         ˙˙··..                                                         .
//     :       ..··˙˙˙˙˙˙··..                                                   .
//    :    .·˙               ˙˙:·..                                             .
//    :  .˙                     ˙. ˙˙··..                                       .
//    : .                         ˙.     ˙˙··..                                 .
//    :.                            ·          ˙˙··..                           .
//    ::                             :               ˙˙··..                     .
//    :       r                       :    R-r             ˙˙··..               .
//    |--------------X----------------+--------------------------::-            .
//    :               \               :                    ..··˙˙               .
//    ::               \             :               ..··˙˙                     .
//    : ·               \           ·          ..··˙˙                           .
//    :  ˙.              \r       .˙     ..··˙˙                                 .
//    :    ˙.             \     .˙ ..··˙˙                                       .
//    :      ˙·.           \ ..··˙˙                                             .
//     :        ˙··.......··˙                                                   .
//     :          ..··˙˙                                                        .
//     :    ..··˙˙                                                              .
//      :·˙˙                                                                    .
namespace editor {

using glm::vec3;

#pragma region Commmands
Toggle_menu_visibility_command::Toggle_menu_visibility_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Hotbar.toggle_visibility"}
    , m_context{context}
{
}

auto Toggle_menu_visibility_command::try_call() -> bool
{
    m_context.hotbar->toggle_mesh_visibility();
    return true;
}

Hotbar_trackpad_command::Hotbar_trackpad_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Hotbar.trackpad"}
    , m_context{context}
{
}

auto Hotbar_trackpad_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    return m_context.hotbar->try_trackpad(input);
}

Hotbar_thumbstick_command::Hotbar_thumbstick_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Hotbar.thumbstick"}
    , m_context{context}
{
}

auto Hotbar_thumbstick_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    // Check for deactivation
    float x = input.variant.vector2.absolute_value.x;
    float y = input.variant.vector2.absolute_value.y;
    if (m_left_active && x > -m_deactivate_threshold) {
        m_left_active = false;
    }
    if (m_right_active && x < m_deactivate_threshold) {
        m_right_active = false;
    }
    if (m_up_active && y < m_deactivate_threshold) {
        m_up_active = false;
    }
    if (m_down_active && y > -m_deactivate_threshold) {
        m_up_active = false;
    }

    // Check for activation
    if (!m_left_active && x < -m_activate_threshold) {
        m_left_active = true;
        m_context.hotbar->rotate_tool(-1);
    }
    if (!m_right_active && x > m_activate_threshold) {
        m_right_active = true;
        m_context.hotbar->rotate_tool(1);
    }
    if (!m_down_active && y < -m_activate_threshold) {
        m_down_active = true;
        // TODO action
    } else if (!m_up_active && y > m_activate_threshold) {
        m_up_active = true;
        // TODO action
    }
    return true;
}

Hotbar_rotate_tool_command::Hotbar_rotate_tool_command(erhe::commands::Commands& commands, Editor_context& context, int rotate_direction)
    : Command{commands, "Hotbar.rotate"}
    , m_context{context}
    , m_rotate_direction{rotate_direction}
{
}

auto Hotbar_rotate_tool_command::try_call() -> bool
{
    m_context.hotbar->rotate_tool(m_rotate_direction);
    return true;
}

#pragma endregion Commands

Hotbar::Hotbar(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus,
    Headset_view&                headset_view,
    Mesh_memory&                 mesh_memory,
    Scene_builder&               scene_builder,
    Tools&                       tools
)
    : erhe::imgui::Imgui_window  {imgui_renderer, imgui_windows, "Hotbar", ""}
    , Tool                       {editor_context}
    , m_toggle_visibility_command{commands, editor_context}
    , m_prev_tool_command        {commands, editor_context, -1}
    , m_next_tool_command        {commands, editor_context, 1}
#if defined(ERHE_XR_LIBRARY_OPENXR)
    , m_trackpad_command         {commands, editor_context}
    , m_trackpad_click_command   {commands, m_trackpad_command}
    , m_thumbstick_command       {commands, editor_context}
#endif
{
    ERHE_PROFILE_FUNCTION();

    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "hotbar");
    ini.get("enabled",    m_enabled);
    ini.get("show",       m_show);
    ini.get("use_radial", m_use_radial);
    ini.get("x",          m_x);
    ini.get("y",          m_y);
    ini.get("z",          m_z);

    if (!m_enabled) {
        hide_window();
        return;
    }

    commands.register_command            (&m_toggle_visibility_command);
    commands.register_command            (&m_prev_tool_command);
    commands.register_command            (&m_next_tool_command);
    commands.bind_command_to_key         (&m_toggle_visibility_command, erhe::window::Key_space, true);
    commands.bind_command_to_menu        (&m_toggle_visibility_command, "View.Hotbar", [&]() -> bool { return m_mesh_visible; });
    commands.bind_command_to_mouse_button(&m_prev_tool_command, erhe::window::Mouse_button_x1, true);
    commands.bind_command_to_mouse_button(&m_next_tool_command, erhe::window::Mouse_button_x2, true);
    m_toggle_visibility_command.set_host(this);
    m_prev_tool_command        .set_host(this);
    m_next_tool_command        .set_host(this);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    commands.register_command(&m_trackpad_command);
    erhe::xr::Headset*    headset  = headset_view.get_headset();
    erhe::xr::Xr_actions* xr_right = (headset != nullptr) ? headset->get_actions_right() : nullptr;
    if (xr_right != nullptr) {
        m_trackpad_click_command.bind(xr_right->trackpad);
        commands.bind_command_to_xr_boolean_action(&m_trackpad_click_command, xr_right->trackpad_click, erhe::commands::Button_trigger::Button_pressed);
        m_trackpad_click_command.set_host(this);
        m_trackpad_command.set_host(this);

        commands.register_command(&m_thumbstick_command);
        commands.bind_command_to_xr_vector2f_action(&m_thumbstick_command, xr_right->thumbstick);
        m_thumbstick_command.set_host(this);
    }
#else
    static_cast<void>(headset_view);
#endif

    set_description("Hotbar");
    set_flags      (Tool_flags::background);
    tools.register_tool(this);

    set_mesh_visibility(m_show);

    auto* scene_root_ptr = scene_builder.get_scene_root().get();
    auto& scene_root = *scene_root_ptr;
    if (m_use_radial) {
        hide_window();
        init_radial_menu(mesh_memory, scene_root);
    } else {
        this->Imgui_window::m_show_in_menu = false;
    }

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );
}

void Hotbar::init_hotbar()
{
    const auto& icon_rasterization = m_context.icon_set->get_hotbar_rasterization();
    const int   icon_size          = icon_rasterization.get_size();
    const auto& tools = m_context.tools->get_tools();
    if (tools.empty()) {
        return;
    }
    int width = icon_size * static_cast<int>(tools.size());

    m_rendertarget_mesh.reset();
    m_rendertarget_node.reset();
    m_rendertarget_imgui_host.reset();

    m_rendertarget_mesh = std::make_shared<Rendertarget_mesh>(
        *m_context.graphics_instance,
        *m_context.mesh_memory,
        width,
        icon_size,
        4000.0f
    );
    const auto scene_root = m_context.scene_builder->get_scene_root();
    m_rendertarget_mesh->layer_id = scene_root->layers().rendertarget()->id;
    m_rendertarget_mesh->enable_flag_bits(erhe::Item_flags::visible | erhe::Item_flags::translucent);

    m_rendertarget_imgui_host = std::make_shared<editor::Rendertarget_imgui_host>(
        *m_context.imgui_renderer,
        *m_context.rendergraph,
        m_context,
        m_rendertarget_mesh.get(),
        "Hotbar imgui host",
        false
    );
    m_rendertarget_imgui_host->set_clear_color(glm::vec4{0.0f, 0.0f, 0.0f, 0.0f});

    m_rendertarget_node = std::make_shared<erhe::scene::Node>("Hotbar RT node");
    m_rendertarget_node->attach(m_rendertarget_mesh);

    ImGuiStyle& style = m_rendertarget_imgui_host->get_mutable_style();
    style.FrameRounding    = 0.0f;
    style.WindowRounding   = 0.0f;
    style.ChildRounding    = 0.0f;
    style.FramePadding     = ImVec2{0.0f, 0.0f};
    style.ItemSpacing      = ImVec2{0.0f, 0.0f};
    style.ItemInnerSpacing = ImVec2{0.0f, 0.0f};
    style.MouseCursorScale = 2.0f;

    this->Hotbar::set_imgui_host(m_rendertarget_imgui_host.get());

    if (m_context.developer_mode) {
        m_rendertarget_mesh->enable_flag_bits(erhe::Item_flags::show_in_ui);
        m_rendertarget_node->enable_flag_bits(erhe::Item_flags::show_in_ui);
    }

    set_mesh_visibility(m_show);
}

void Hotbar::init_radial_menu(Mesh_memory& mesh_memory, Scene_root&  scene_root)
{
    const float outer_radius = 1.0f;
    const float inner_radius = 0.125f;
    const int   slice_count  = 100;
    const int   stack_count  = 2;

    auto disc_material = std::make_shared<erhe::primitive::Material>("Circular Menu Disc", glm::vec4{0.1f, 0.2f, 0.3f, 1.0f});
    disc_material->opacity = 0.5f;

    GEO::Mesh disc_geo_mesh_shared;
    erhe::geometry::shapes::make_disc(
        disc_geo_mesh_shared,
        outer_radius,
        inner_radius,
        slice_count,
        stack_count,
        20,
        30,
        0,
        2
    );

    erhe::primitive::Element_mappings dummy; // TODO make Element_mappings optional
    erhe::primitive::Buffer_mesh buffer_mesh{};
    const bool buffer_mesh_ok = erhe::primitive::build_buffer_mesh(
        buffer_mesh,
        disc_geo_mesh_shared,
        erhe::primitive::Build_info{
            .primitive_types = { .fill_triangles = true },
            .buffer_info     = mesh_memory.buffer_info
        },
        dummy
    );
    ERHE_VERIFY(buffer_mesh_ok); // TODO

    m_radial_menu_background_mesh = std::make_shared<erhe::scene::Mesh>(
        "Radiaul Menu Mesh",
        erhe::primitive::Primitive{buffer_mesh, disc_material}
    );

    erhe::scene::Scene* scene = scene_root.get_hosted_scene();
    const auto root_node = scene->get_root_node();
    ERHE_VERIFY(scene != nullptr);
    ERHE_VERIFY(root_node);
    m_radial_menu_background_mesh->layer_id = scene_root.layers().content()->id;

    m_radial_menu_background_mesh->enable_flag_bits(
        erhe::Item_flags::content     |
        erhe::Item_flags::visible     |
        erhe::Item_flags::translucent |
        erhe::Item_flags::show_in_ui
    );

    m_radial_menu_node = std::make_shared<erhe::scene::Node>("Radial menu node");
    m_radial_menu_node->attach(m_radial_menu_background_mesh);
    m_radial_menu_node->enable_flag_bits(
        erhe::Item_flags::content    |
        erhe::Item_flags::visible    |
        erhe::Item_flags::show_in_ui
    );
}

void Hotbar::get_all_tools()
{
    ERHE_PROFILE_FUNCTION();

    m_slot_first = 0;
    m_slot_last  = 0;
    m_slots.clear();
    const auto& tools = m_context.tools->get_tools();
    for (Tool* tool : tools) {
        const auto opt_icon = tool->get_icon();
        if (!opt_icon.has_value()) {
            continue;
        }
        if (erhe::bit::test_all_rhs_bits_set(tool->get_flags(), Tool_flags::toolbox)) {
            m_slot_last = m_slots.size();
            m_slots.push_back(tool);
        }
    }
    init_hotbar();
}

void Hotbar::on_message(Editor_message& message)
{
    Scene_view* const old_scene_view = get_hover_scene_view();
    Tool::on_message(message);

    if (!m_enabled || !m_show) {
        return;
    }

    using namespace erhe::bit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_tool_select)) {
        update_slot_from_tool(m_context.tools->get_priority_tool());
    }

    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
        if (message.scene_view != old_scene_view) {
            if (m_use_radial) {
                update_node_transform();
            } else {
                using Rendergraph_node = erhe::rendergraph::Rendergraph_node;
                auto old_node = (old_scene_view     != nullptr) ? old_scene_view    ->get_rendergraph_node() : nullptr;
                auto new_node = (message.scene_view != nullptr) ? message.scene_view->get_rendergraph_node() : nullptr;
                if (old_node != new_node) {
                    if (old_node) {
                        m_context.rendergraph->disconnect(erhe::rendergraph::Rendergraph_node_key::rendertarget_texture, m_rendertarget_imgui_host.get(), old_node);
                    }
                    set_mesh_visibility(static_cast<bool>(new_node));
                    if (new_node) {
                        m_context.rendergraph->connect(erhe::rendergraph::Rendergraph_node_key::rendertarget_texture, m_rendertarget_imgui_host.get(), new_node);
                    }
                }
            }
        }
    }

    // Update rendertarget node transform to match render camera.
    // This is used only for horizontal hotbar, not for radial menu.
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_render_scene_view)) {
        bool visible = message.scene_view && (get_hover_scene_view() == message.scene_view);
        set_mesh_visibility(visible);
        if (!m_use_radial) {
            update_node_transform();
        }
    }
}

auto Hotbar::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_context.OpenXR) {
        return m_context.headset_view->get_camera();
    }
#endif
    const auto viewport_scene_view = m_context.scene_views->hover_scene_view();
    if (!viewport_scene_view) {
        return {};
    }
    return viewport_scene_view->get_camera();
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

        // Lookat creates transform which looks along negative Z.
        // Rotate around local Y.
        constexpr glm::mat4 rotate{
            -1.0f, 0.0f, 0.0f, 0.0f,
             0.0f, 1.0f, 0.0f, 0.0f,
             0.0f, 0.0f,-1.0f, 0.0f,
             0.0f, 0.0f, 0.0f, 1.0f
        };

        world_from_node = erhe::math::create_look_at(
            glm::vec3{world_from_camera * glm::vec4{m_x, m_y, m_z, 1.0}}, // eye
            glm::vec3{world_from_camera * glm::vec4{0.0, 0.0, 0.0, 1.0}}, // target
            glm::vec3{world_from_camera * glm::vec4{0.0, 1.0, 0.0, 0.0}}  // up
        ) * rotate;

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
            m_radial_menu_node->set_parent(root_node);
        }
        m_radial_menu_node->set_world_from_node(world_from_node);
    }
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
        ImGuiWindowFlags_NoCollapse        |
        ImGuiWindowFlags_NoNavInputs       |
        ImGuiWindowFlags_NoNavFocus        |
        ImGuiWindowFlags_NoDocking;
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
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
}

void Hotbar::on_end()
{
    ImGui::PopStyleVar(4);
}

void Hotbar::handle_slot_update()
{
    if ((m_slot >= m_slot_first) && (m_slot <= m_slot_last)) {
        Tool* new_tool = m_slots.at(m_slot);
        m_context.tools->set_priority_tool(new_tool);
    }
}

auto Hotbar::try_trackpad(erhe::commands::Input_arguments& input) -> bool
{
    // TODO move most of this logic to Hotbar_trackpad_command,
    //      should use Hotbar::rotate_tool() from there.
    if (!m_enabled) {
        return false;
    }

    const auto position = input.variant.vector2.absolute_value;
    if (position.x < -0.2f) {
        rotate_tool(-1);
        m_slot = (m_slot == m_slot_first) ? m_slot_last : m_slot - 1;
    }

    if (position.x > 0.2f) {
        rotate_tool(1);
    }

    return true;
}

void Hotbar::rotate_tool(int direction)
{
    const auto old_slot = m_slot;
    if (direction < 0) {
        m_slot = (m_slot == m_slot_first) ? m_slot_last : m_slot - 1;
    } else if (direction > 0) {
        m_slot = (m_slot == m_slot_last) ? m_slot_first : m_slot + 1;
    }
    if (m_slot != old_slot) {
        handle_slot_update();
    }
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
    const auto&     icon_rasterization{m_context.icon_set->get_hotbar_rasterization()};
    const bool      is_boosted        {tool->get_priority_boost() > 0};
    const auto      opt_icon          {tool->get_icon()};

    if (!opt_icon) {
        return;
    }

    if (is_boosted) {
        ImGui::PushStyleColor(ImGuiCol_Button, m_color_active);
    }

    const bool is_pressed = icon_rasterization.icon_button(id, opt_icon.value(), background_color, tint_color);

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tool->get_description());
    }

    if (is_boosted) {
        ImGui::PopStyleColor();
    }

    if (is_pressed) {
        m_context.tools->set_priority_tool(is_boosted ? nullptr : tool);
    }
}

void Hotbar::update_slot_from_tool(Tool* tool)
{
    if (tool == nullptr) {
        return;
    }
    for (size_t i = 0, end = m_slots.size(); i < end; ++i) {
        if (m_slots.at(i) == tool) {
            m_slot = i;
            return;
        }
    }
}

void Hotbar::imgui()
{
    ERHE_PROFILE_FUNCTION();
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PushID("Hotbar::imgui");
    //const ImVec2 button_size{64.0f, 64.0f};
    //ImGui::SliderFloat("X", &m_x, -2.0f, 2.0f);
    //ImGui::SliderFloat("Y", &m_y, -0.3f, 0.0f);
    //ImGui::SliderFloat("Z", &m_z, -2.0f, 2.0f);

    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  m_color_active);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_color_hover);
    ImGui::PushStyleColor(ImGuiCol_Button,        m_color_inactive);
    ImVec2 cursor_position = ImGui::GetCursorPos();
    static_cast<void>(cursor_position);

    uint32_t id = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0f, 0.0f});
    for (auto* tool : m_slots) {
        tool_button(++id, tool);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::PopID();
#endif
}

auto Hotbar::toggle_mesh_visibility() -> bool
{
    if (!m_enabled) {
        return false;
    }

    update_node_transform();

    set_mesh_visibility(!m_mesh_visible);
    return m_mesh_visible;
}

void Hotbar::set_mesh_visibility(const bool value)
{
    if (m_rendertarget_node) {
        m_rendertarget_node->set_visible(value);
    }
    Imgui_window::set_window_visibility(value);

    if (m_rendertarget_mesh) {
        m_rendertarget_imgui_host->set_enabled(value);
        m_rendertarget_mesh->set_visible(value);
        log_hud->trace("Horizontal menu visibility set to {}", value);
    }

    if (m_radial_menu_background_mesh) {
        m_radial_menu_background_mesh->set_visible(value);
        log_hud->trace("Radial menu visibility set to {}", value);
    }
}

} // namespace editor
