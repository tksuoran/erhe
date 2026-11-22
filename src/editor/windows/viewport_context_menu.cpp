#include "windows/viewport_context_menu.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "editor_log.hpp"
#include "level/level_kits.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/selection_tool.hpp"
#include "windows/item_tree_window.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <imgui/imgui.h>

namespace editor {

namespace {

constexpr const char* c_popup_name = "Viewport context menu";

}

Viewport_context_menu_command::Viewport_context_menu_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Viewport.context_menu"}
    , m_context{context}
{
}

void Viewport_context_menu_command::try_ready()
{
    if (m_context.viewport_context_menu->try_ready()) {
        set_ready();
    }
}

auto Viewport_context_menu_command::try_call() -> bool
{
    if (get_command_state() != erhe::commands::State::Ready) {
        return false;
    }
    const bool consumed = m_context.viewport_context_menu->open();
    set_inactive();
    return consumed;
}

Viewport_context_menu::Viewport_context_menu(erhe::commands::Commands& commands, App_context& context)
    : m_command{commands, context}
    , m_context{context}
{
    commands.register_command(&m_command);
    // A click: pointer motion between the press and the release leaves the button to the drags bound to it.
    commands.bind_command_to_mouse_button(&m_command, erhe::window::Mouse_button_right, erhe::commands::Button_trigger::Button_released);
    m_command.set_host(this);
}

auto Viewport_context_menu::get_hover_scene_view() const -> std::shared_ptr<Viewport_scene_view>
{
    if (m_context.scene_views == nullptr) {
        return {};
    }
    std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_context.scene_views->hover_scene_view();
    if (!viewport_scene_view) {
        return {};
    }
    const std::shared_ptr<Scene_root> scene_root = viewport_scene_view->get_scene_root();
    if (!scene_root || (scene_root->get_browser_window() == nullptr)) {
        return {};
    }
    // A tool handle or a rendertarget's own UI under the pointer takes the click.
    if (
        viewport_scene_view->get_hover(Hover_entry::tool_slot).valid ||
        viewport_scene_view->get_hover(Hover_entry::rendertarget_slot).valid
    ) {
        return {};
    }
    return viewport_scene_view;
}

auto Viewport_context_menu::try_ready() const -> bool
{
    return static_cast<bool>(get_hover_scene_view());
}

auto Viewport_context_menu::open() -> bool
{
    const std::shared_ptr<Viewport_scene_view> viewport_scene_view = get_hover_scene_view();
    if (!viewport_scene_view) {
        return false;
    }
    const std::shared_ptr<Scene_root> scene_root = viewport_scene_view->get_scene_root();
    const erhe::Item_host* const      host       = scene_root.get();

    const Hover_entry& content = viewport_scene_view->get_hover(Hover_entry::content_slot);
    std::shared_ptr<erhe::Item_base> hovered_item;
    if (content.valid) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = content.scene_mesh_weak.lock();
        hovered_item = mesh ? std::shared_ptr<erhe::Item_base>{mesh} : content.item_weak.lock();
    }

    // The selection of this scene wins over the hovered item: its entries then act on the selection.
    std::shared_ptr<erhe::Item_base> target;
    if (hovered_item && hovered_item->is_selected()) {
        target = hovered_item;
    } else {
        const auto is_target = [host](const std::shared_ptr<erhe::Item_base>& item) -> bool {
            return item && item->is_selected() && (item->get_item_host() == host) && (dynamic_cast<const erhe::Hierarchy*>(item.get()) != nullptr);
        };
        const std::shared_ptr<erhe::Item_base> active_item = m_context.selection->get_active_item();
        if (is_target(active_item)) {
            target = active_item;
        } else {
            const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();
            for (auto i = selected_items.rbegin(), end = selected_items.rend(); i != end; ++i) {
                if (is_target(*i)) {
                    target = *i;
                    break;
                }
            }
        }
    }
    if (!target) {
        target = hovered_item;
    }

    // FD2.4: the surface point, foliage passed through; its normal faces the side the pointer ray came from.
    m_placement.reset();
    const std::optional<Hover_placement_hit> placement = m_context.level_kits->get_placement_hover(*viewport_scene_view);
    if (placement.has_value()) {
        m_placement = Hover_placement{.position = placement.value().position, .normal = placement.value().normal};
    }

    m_viewport_scene_view = viewport_scene_view;
    m_scene_root          = scene_root;
    m_item                = target;
    m_has_item            = static_cast<bool>(target);
    m_open_pending        = true;
    m_shown               = false;
    log_scene_view->debug(
        "Viewport context menu: scene '{}', target '{}', {}",
        scene_root->get_name(),
        target ? target->get_name() : std::string{"(empty area)"},
        m_placement.has_value() ? "at a hovered point" : "no hovered point"
    );
    return true;
}

void Viewport_context_menu::reset()
{
    m_viewport_scene_view.reset();
    m_scene_root.reset();
    m_item.reset();
    m_placement.reset();
    m_has_item     = false;
    m_open_pending = false;
    m_shown        = false;
}

void Viewport_context_menu::imgui(const Viewport_scene_view& viewport_scene_view)
{
    if (!m_open_pending && !m_shown) {
        return;
    }
    if (m_viewport_scene_view.lock().get() != &viewport_scene_view) {
        return;
    }
    if (m_open_pending) {
        ImGui::OpenPopup(c_popup_name);
        m_open_pending = false;
        m_shown        = true;
    }

    // The viewport's window has no padding; the menu takes the Hierarchy menu's.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});
    const bool begin = ImGui::BeginPopup(
        c_popup_name,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings
    );
    if (begin) {
        // The scene may have closed and the item may have left it (an undo) while the menu was open.
        const std::shared_ptr<Scene_root>      scene_root = m_scene_root.lock();
        const std::shared_ptr<erhe::Item_base> item       = m_item.lock();
        Item_tree_window* const tree =
            (scene_root && m_context.app_scenes->is_host_registered(scene_root.get()))
                ? scene_root->get_browser_window()
                : nullptr;
        const bool item_valid = !m_has_item || (item && (item->get_item_host() == scene_root.get()));
        if ((tree != nullptr) && item_valid) {
            bool close{false};
            if (m_has_item) {
                tree->item_context_menu_entries(item, Context_menu_site::viewport, m_placement, close);
            } else {
                tree->root_context_menu_entries(m_placement, close);
            }
            tree->queue_context_menu_operations();
        } else {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else {
        reset();
    }
    ImGui::PopStyleVar(1);
}

}
