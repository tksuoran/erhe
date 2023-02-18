#include "tools/selection_tool.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "operations/compound_operation.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/render_context.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tools.hpp"
#include "tools/trs/trs_tool.hpp"
#include "tools/trs/move_tool.hpp"
#include "tools/trs/rotate_tool.hpp"
#include "windows/node_tree_window.hpp"
#include "windows/viewport_config_window.hpp"

#include "erhe/application/application_view.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/commands/command_binding.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/time.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe/xr/xr_action.hpp"
#   include "erhe/xr/headset.hpp"
#endif

namespace editor
{

using glm::mat4;
using glm::vec3;
using glm::vec4;

#pragma region Range_selection
Range_selection::Range_selection() = default;

void Range_selection::set_terminator(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    if (!m_primary_terminator) {
        log_selection->trace(
            "setting primary terminator to {} {}",
            item->type_name(),
            item->get_name()
        );

        m_primary_terminator = item;
        m_edited = true;
        return;
    }
    if (item == m_primary_terminator) {
        log_selection->trace(
            "ignoring setting terminator to {} - {} because it is already the primary terminator",
            item->type_name(),
            item->get_name()
        );
        return;
    }
    if (item == m_secondary_terminator) {
        log_selection->trace(
            "ignoring setting terminator to {} - {} because it is already the secondary terminator",
            item->type_name(),
            item->get_name()
        );
        return;
    }

    log_selection->trace("setting secondary terminator to {} {}", item->type_name(), item->get_name());
    m_secondary_terminator = item;
    m_edited = true;
}

void Range_selection::entry(
    const std::shared_ptr<erhe::scene::Item>& item,
    const bool                                attachments_expanded
)
{
    m_entries.push_back(item);
    if (!attachments_expanded) {
        const auto& node = as_node(item);
        if (node && !attachments_expanded) {
            for (const auto& attachment : node->attachments()) {
                m_entries.push_back(attachment);
            }
        }
    }
}

void Range_selection::begin()
{
    m_edited = false;
    m_entries.clear();
}

void Range_selection::end(const bool attachments_expanded)
{
    if (m_entries.empty() || !m_edited || !m_primary_terminator || !m_secondary_terminator) {
        m_entries.clear();
        return;
    }
    log_selection->trace("setting selection since range was modified");

    std::vector<std::shared_ptr<erhe::scene::Item>> selection;
    bool between_terminators{false};

    auto primary_terminator   = m_primary_terminator;
    auto secondary_terminator = m_secondary_terminator;
    auto primary_node         = as_node(m_primary_terminator);
    auto secondary_node       = as_node(m_secondary_terminator);

    for (const auto& item : m_entries) {
        const bool match_primary   = item == primary_terminator;
        const bool match_secondary = item == secondary_terminator;
        if (match_primary || match_secondary) {
            log_selection->trace("   T. {} {} {}", item->type_name(), item->get_name(), item->get_id());
            selection.push_back(item);
            between_terminators = !between_terminators;
            if (!attachments_expanded && between_terminators) {
                if (match_secondary && primary_node && !primary_node->attachments().empty()) {
                    primary_terminator = primary_node->attachments().back();
                }
                if (match_primary && secondary_node && !secondary_node->attachments().empty()) {
                    secondary_terminator = secondary_node->attachments().back();
                }
            } else {
                primary_terminator   = m_primary_terminator;
                secondary_terminator = m_secondary_terminator;
            }
            continue;
        }
        if (between_terminators) {
            log_selection->trace("    + {} {} {}", item->type_name(), item->get_name(), item->get_id());
            selection.push_back(item);
        } else {
            log_selection->trace("    - {} {} {}", item->type_name(), item->get_name(), item->get_id());
        }
    }
    if (selection.empty()) {
        m_entries.clear();
        return;
    }

    g_selection_tool->set_selection(selection);
    m_entries.clear();
}

void Range_selection::reset()
{
    log_selection->trace("resetting range selection");
    if (m_primary_terminator && m_secondary_terminator) {
        g_selection_tool->clear_selection();
    }
    m_primary_terminator.reset();
    m_secondary_terminator.reset();
}

#pragma endregion Range_selection
#pragma region Commands
Selection_tool_select_command::Selection_tool_select_command()
    : Command{"Selection_tool.select"}
{
}

void Selection_tool_select_command::try_ready()
{
    if (g_selection_tool->on_select_try_ready()) {
        set_ready();
    }
}

auto Selection_tool_select_command::try_call() -> bool
{
    if (get_command_state() != erhe::application::State::Ready) {
        log_selection->trace("selection tool not in ready state");
        return false;
    }

    const bool consumed = g_selection_tool->on_select();
    set_inactive();
    return consumed;
}

//

Selection_tool_select_toggle_command::Selection_tool_select_toggle_command()
    : Command{"Selection_tool.select_toggle"}
{
}

void Selection_tool_select_toggle_command::try_ready()
{
    if (g_selection_tool->on_select_try_ready()) {
        set_ready();
    }
}

auto Selection_tool_select_toggle_command::try_call() -> bool
{
    if (get_command_state() != erhe::application::State::Ready) {
        log_selection->trace("selection tool not in ready state");
        return false;
    }

    const bool consumed = g_selection_tool->on_select_toggle();
    set_inactive();
    return consumed;
}

//

Selection_tool_delete_command::Selection_tool_delete_command()
    : Command{"Selection_tool.delete"}
{
}

auto Selection_tool_delete_command::try_call() -> bool
{
    return g_selection_tool->delete_selection();
}

#pragma endregion Commands


Selection_tool* g_selection_tool{nullptr};

Selection_tool::Selection_tool()
    : erhe::application::Imgui_window{c_title}
    , erhe::components::Component    {c_type_name}
{
}

Selection_tool::~Selection_tool() noexcept
{
    ERHE_VERIFY(g_selection_tool == nullptr);
}

void Selection_tool::deinitialize_component()
{
    ERHE_VERIFY(g_selection_tool == this);
    m_select_command.set_host(nullptr);
    m_delete_command.set_host(nullptr);
    m_selection.clear();
    m_range_selection.reset();
    m_hover_mesh.reset();
    g_selection_tool = nullptr;
}

void Selection_tool::declare_required_components()
{
    require<erhe::application::Commands>();
    require<erhe::application::Imgui_windows>();
    require<Editor_message_bus>();
    require<Icon_set          >();
    require<Tools             >();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    require<Headset_view      >();
#endif
}

void Selection_tool::initialize_component()
{
    ERHE_VERIFY(g_selection_tool == nullptr);

    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::toolbox | Tool_flags::secondary);
    set_icon         (g_icon_set->icons.select);
    g_tools->register_tool(this);

    erhe::application::g_imgui_windows->register_imgui_window(this, "selection_tool");
    hide();

    auto& commands = *erhe::application::g_commands;
    commands.register_command            (&m_select_command);
    commands.register_command            (&m_delete_command);
    commands.bind_command_to_mouse_button(&m_select_command, erhe::toolkit::Mouse_button_left, false);
    commands.bind_command_to_key         (&m_delete_command, erhe::toolkit::Key_delete,        true);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    const auto* headset = g_headset_view->get_headset();
    if (headset != nullptr) {
        auto& xr_left  = headset->get_actions_left();
        auto& xr_right = headset->get_actions_right();
        commands.bind_command_to_xr_boolean_action(&m_select_command,        xr_right.trigger_click, erhe::application::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_boolean_action(&m_select_command,        xr_left .x_click,       erhe::application::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_boolean_action(&m_select_toggle_command, xr_right.a_click,       erhe::application::Button_trigger::Button_pressed);
    }
#endif

    g_editor_message_bus->add_receiver(
        [&](Editor_message& message)
        {
            Tool::on_message(message);
        }
    );

    m_select_command.set_host(this);
    m_delete_command.set_host(this);

    g_selection_tool = this;
}

void Selection_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (new_priority < old_priority) {
        clear_selection();
    }
}

auto Selection_tool::selection() const -> const std::vector<std::shared_ptr<erhe::scene::Item>>&
{
    return m_selection;
}

auto Selection_tool::delete_selection() -> bool
{
    if (m_selection.empty()) {
        return false;
    }

    //const auto& scene_root = m_editor_scenes->get_scene_root();
    Compound_operation::Parameters compound_parameters;
    for (auto& item : m_selection) {
        auto node = as_node(item);
        if (!node) {
            // TODO
            continue;
        }
        compound_parameters.operations.push_back(
            std::make_shared<Node_insert_remove_operation>(
                Node_insert_remove_operation::Parameters{
                    .node           = node,
                    .parent         = node->parent().lock(),
                    .mode           = Scene_item_operation::Mode::remove,
                }
            )
        );
    }
    if (compound_parameters.operations.empty()) {
        return false;
    }

    const auto op = std::make_shared<Compound_operation>(std::move(compound_parameters));
    g_operation_stack->push(op);
    return true;
}

auto Selection_tool::range_selection() -> Range_selection&
{
    return m_range_selection;
}

auto Selection_tool::get_first_selected_node() -> std::shared_ptr<erhe::scene::Node>
{
    for (const auto& i : m_selection) {
        if (is_node(i)) {
            return as_node(i);
        }
    }
    return {};
}

auto Selection_tool::get_first_selected_scene() -> std::shared_ptr<erhe::scene::Scene>
{
    for (const auto& i : m_selection) {
        if (is_scene(i)) {
            return as_scene(i);
        }
    }
    return {};
}

template <typename T>
[[nodiscard]] auto is_in(
    const T&              item,
    const std::vector<T>& items
) -> bool
{
    return std::find(
        items.begin(),
        items.end(),
        item
    ) != items.end();
}

void Selection_tool::set_selection(const std::vector<std::shared_ptr<erhe::scene::Item>>& selection)
{
    for (auto& item : m_selection) {
        if (
            item->is_selected() &&
            !is_in(item, selection)
        ) {
            item->set_selected(false);
        }
    }
    for (auto& item : selection) {
        item->set_selected(true);
    }
    m_selection = selection;
    send_selection_change_message();
}

auto Selection_tool::on_select_try_ready() -> bool
{
    auto* scene_view = get_hover_scene_view();
    if (!scene_view) {
        return false;
    }

    const auto& content      = scene_view->get_hover(Hover_entry::content_slot);
    const auto& tool         = scene_view->get_hover(Hover_entry::tool_slot);
    const auto& rendertarget = scene_view->get_hover(Hover_entry::rendertarget_slot);
    m_hover_mesh    = content.mesh;
    m_hover_content = content.valid;
    m_hover_tool    = tool.valid;

    if (m_hover_content && !m_hover_tool && !rendertarget.valid) {
        log_selection->trace("Can select");
        return true;
    } else {
        if (!m_hover_content) {
            log_selection->trace("Cannot select: Not hovering over content");
        }
        if (!m_hover_tool) {
            log_selection->trace("Cannot select: Hovering over tool");
        }
        if (rendertarget.valid) {
            log_selection->trace("Cannot select: Hovering over rendertarget");
        }
    }
    return false;
}

auto Selection_tool::on_select() -> bool
{
    const bool was_selected = is_in_selection(m_hover_mesh);
    if (erhe::application::g_view->control_key_down()) {
        if (m_hover_content) {
            toggle_mesh_selection(m_hover_mesh, was_selected, false);
            return true;
        }
        return false;
    }

    if (!m_hover_content) {
        clear_selection();
    } else {
        m_range_selection.reset();
        toggle_mesh_selection(m_hover_mesh, was_selected, true);
    }
    return true;
}

auto Selection_tool::on_select_toggle() -> bool
{
    if (m_hover_content) {
        const bool was_selected = is_in_selection(m_hover_mesh);
        toggle_mesh_selection(m_hover_mesh, was_selected, false);
        return true;
    }
    return false;
}

auto Selection_tool::clear_selection() -> bool
{
    if (m_selection.empty()) {
        return false;
    }

    for (const auto& item : m_selection) {
        ERHE_VERIFY(item);
        if (!item) {
            continue;
        }
        item->set_selected(false);
    }

    log_selection->trace("Clearing selection ({} items were selected)", m_selection.size());
    m_selection.clear();
    m_range_selection.reset();
    sanity_check();
    send_selection_change_message();
    return true;
}

void Selection_tool::toggle_mesh_selection(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    const bool                                was_selected,
    const bool                                clear_others
)
{
    bool add{false};
    bool remove{false};
    if (clear_others) {
        clear_selection();
        if (!was_selected && mesh) {
            add = true;
        }
    } else if (mesh) {
        if (was_selected) {
            remove = true;
        } else {
            add = true;
        }
    }

    ERHE_VERIFY(!add || !remove);

    if (add) {
        if (g_node_tree_window->expand_attachments()) {
            add_to_selection(mesh);
            m_range_selection.set_terminator(mesh);
        } else {
            erhe::scene::Node* const node = mesh->get_node();
            const auto node_item = node->shared_from_this();
            add_to_selection(node_item);
            m_range_selection.set_terminator(node_item);
            for (auto& attachment : node->attachments()) {
                add_to_selection(attachment);
                m_range_selection.set_terminator(attachment);
            }
        }
    } else if (remove) {
        if (g_node_tree_window->expand_attachments()) {
            remove_from_selection(mesh);
        } else {
            erhe::scene::Node* const node = mesh->get_node();
            const auto node_item = node->shared_from_this();
            remove_from_selection(node_item);
            m_range_selection.set_terminator(node_item);
            for (auto& attachment : node->attachments()) {
                remove_from_selection(attachment);
            }
        }
    }

    send_selection_change_message();
}

auto Selection_tool::is_in_selection(
    const std::shared_ptr<erhe::scene::Item>& item
) const -> bool
{
    if (!item) {
        return false;
    }

    return std::find(
        m_selection.begin(),
        m_selection.end(),
        item
    ) != m_selection.end();
}

    auto Selection_tool::add_to_selection(
    const std::shared_ptr<erhe::scene::Item>& item
) -> bool
{
    if (!item) {
        log_selection->warn("Trying to add empty item to selection");
        return false;
    }

    item->set_selected(true);

    if (!is_in_selection(item)) {
        log_selection->trace("Adding {} to selection", item->get_name());
        m_selection.push_back(item);
        send_selection_change_message();
        return true;
    }

    log_selection->warn("Adding {} to selection failed - was already in selection", item->get_name());
    return false;
}

auto Selection_tool::remove_from_selection(
    const std::shared_ptr<erhe::scene::Item>& item
) -> bool
{
    if (!item) {
        log_selection->warn("Trying to remove empty item from selection");
        return false;
    }

    item->set_selected(false);

    const auto i = std::remove(
        m_selection.begin(),
        m_selection.end(),
        item
    );
    if (i != m_selection.end()) {
        log_selection->trace("Removing item {} from selection", item->get_name());
        m_selection.erase(i, m_selection.end());
        send_selection_change_message();
        return true;
    }

    log_selection->trace("Removing item {} from selection failed - was not in selection", item->get_name());
    return false;
}

void Selection_tool::update_selection_from_scene_item(
    const std::shared_ptr<erhe::scene::Item>& item,
    const bool                                added
)
{
    if (item->is_selected() && added) {
        if (!is_in(item, m_selection)) {
            m_selection.push_back(item);
            send_selection_change_message();
        }
    } else {
        if (is_in(item, m_selection)) {
            const auto i = std::remove(
                m_selection.begin(),
                m_selection.end(),
                item
            );
            if (i != m_selection.end()) {
                m_selection.erase(i, m_selection.end());
                send_selection_change_message();
            }
        }
    }
}

void Selection_tool::sanity_check()
{
#if !defined(NDEBUG)
    std::size_t error_count{0};

    const auto& scene_roots = g_editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots) {
        const auto& scene = scene_root->scene();
        const auto& flat_nodes = scene.get_flat_nodes();
        for (const auto& node : flat_nodes) {
            const auto item = std::static_pointer_cast<erhe::scene::Item>(node);
            if (
                node->is_selected() &&
                !is_in(item, m_selection)
            ) {
                log_selection->error("Node has selection flag set without being in selection");
                ++error_count;
            } else if (
                !node->is_selected() &&
                is_in(item, m_selection)
            ) {
                log_selection->error("Node does not have selection flag set while being in selection");
                ++error_count;
            }
        }
    }

    if (error_count > 0) {
        log_selection->error("Selection errors: {}", error_count);
    }
#endif
}

void Selection_tool::send_selection_change_message() const
{
    g_editor_message_bus->send_message(
        Editor_message{
            .update_flags = Message_flag_bit::c_flag_bit_selection
        }
    );
}

void Selection_tool::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    for (const auto& item : m_selection) {
        if (!item) {
            ImGui::BulletText("(empty)");
        } else {
            ImGui::BulletText(
                "%s %s %s",
                item->type_name(),
                item->get_name().c_str(),
                item->is_selected() ? "Ok" : "?!"
            );
        }
    }
#endif
}

void Selection_tool::viewport_toolbar(bool& hovered)
{
    ImGui::PushID("Selection_tool::viewport_toolbar");
    const auto& icon_rasterication = g_icon_set->get_small_rasterization();

    int boost = get_priority_boost();
    const auto mode = boost > 0
        ? erhe::application::Item_mode::active
        : erhe::application::Item_mode::normal;

    erhe::application::begin_button_style(mode);
    const bool button_pressed = icon_rasterication.icon_button(
        ERHE_HASH("select"),
        g_icon_set->icons.select,
        glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        false
    );
    erhe::application::end_button_style(mode);
    if (ImGui::IsItemHovered()) {
        hovered = true;
        ImGui::SetTooltip(
            boost > 0
                ? "De-prioritize Selection Tool"
                : "Prioritize Selection Tool"
        );
    }
    if (button_pressed)
    {
        set_priority_boost(boost == 0 ? 10 : 0);
    }
    ImGui::PopID();
}

}
