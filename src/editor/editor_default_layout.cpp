#include "editor_default_layout.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "windows/viewport_window.hpp"

#include "config/generated/default_layout_config.hpp"
#include "config/generated/default_layout_config_serialization.hpp"
#include "config/generated/dock_placement_serialization.hpp"

#include "erhe_codegen/config_io.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/window_imgui_host.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace editor {

namespace {

constexpr const char* c_default_layout_file_path      = "config/editor/default_layout.json";
constexpr const char* c_primary_viewport_substitution = "$primary_viewport";

// Finds a registered Imgui_window by its ImGui title.
auto find_window_by_title(App_context& context, const std::string& title) -> erhe::imgui::Imgui_window*
{
    if (context.imgui_windows == nullptr) {
        return nullptr;
    }
    std::vector<erhe::imgui::Imgui_window*>& windows = context.imgui_windows->get_windows();
    for (erhe::imgui::Imgui_window* window : windows) {
        if (window->get_title() == title) {
            return window;
        }
    }
    return nullptr;
}

// Best-effort lookup of the primary viewport window title. The Viewport_window's
// ImGui title is composed as "{name}###Viewport_window {slot}" by
// Scene_views::create_viewport_window; the "###" suffix carries the stable
// window ID, so this title keeps addressing the same window even after the
// visible part is retitled to the shown scene's name.
auto find_primary_viewport_title(App_context& context) -> std::string
{
    if (context.imgui_windows == nullptr) {
        return {};
    }
    std::vector<erhe::imgui::Imgui_window*>& windows = context.imgui_windows->get_windows();
    for (erhe::imgui::Imgui_window* window : windows) {
        if (dynamic_cast<Viewport_window*>(window) != nullptr) {
            return window->get_title();
        }
    }
    return {};
}

[[nodiscard]] auto to_imgui_dir(const Dock_direction direction) -> ImGuiDir
{
    switch (direction) {
        case Dock_direction::left:  return ImGuiDir_Left;
        case Dock_direction::right: return ImGuiDir_Right;
        case Dock_direction::up:    return ImGuiDir_Up;
        case Dock_direction::down:  return ImGuiDir_Down;
        case Dock_direction::none:
        default:                    return ImGuiDir_None;
    }
}

// Applies the placement list. Entries are applied in order; each entry places
// one window relative to a window placed by an earlier entry (or relative to
// the root dockspace when target is empty):
//  - direction none: the window becomes a tab in the target's dock node.
//  - any other direction: the target's dock node is split and this window
//    takes `fraction` of the ROOT DOCKSPACE size (width for left/right,
//    height for up/down) on the given side. The fraction is translated to
//    the local split ratio, so repeatedly splitting off the same window
//    keeps the stated fractions comparable across entries.
void build_default_layout(
    const ImGuiID                      root_dock_id,
    const ImVec2                       available_size,
    const std::vector<Dock_placement>& placements
)
{
    ImGui::DockBuilderRemoveNode (root_dock_id);
    ImGui::DockBuilderAddNode    (root_dock_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root_dock_id, available_size);

    // Tracks which dock node each already-placed window currently lives in,
    // so later entries can target earlier windows. Splitting a node moves the
    // windows previously docked there into the "remaining" child node.
    std::map<std::string, ImGuiID> window_dock_node;

    // Tracks the fraction of the root dockspace (x = width, y = height) each
    // known node occupies, so placement fractions can be stated relative to
    // the root and translated into the local split ratio DockBuilder needs.
    std::map<ImGuiID, ImVec2> node_root_fraction;
    node_root_fraction[root_dock_id] = ImVec2{1.0f, 1.0f};

    for (const Dock_placement& placement : placements) {
        if (placement.window.empty()) {
            continue;
        }
        ImGuiID target_node_id = root_dock_id;
        if (!placement.target.empty()) {
            const auto it = window_dock_node.find(placement.target);
            if (it == window_dock_node.end()) {
                log_startup->warn(
                    "Default layout: dock target '{}' for window '{}' has not been placed",
                    placement.target,
                    placement.window
                );
                continue;
            }
            target_node_id = it->second;
        }

        const ImGuiDir direction = to_imgui_dir(placement.direction);
        if (direction == ImGuiDir_None) {
            ImGui::DockBuilderDockWindow(placement.window.c_str(), target_node_id);
            window_dock_node[placement.window] = target_node_id;
        } else {
            // Translate the root-relative fraction into the local split ratio
            // of the target node along the split axis.
            const ImVec2 target_fraction  = node_root_fraction[target_node_id];
            const bool   horizontal       = (direction == ImGuiDir_Left) || (direction == ImGuiDir_Right);
            const float  target_axis      = horizontal ? target_fraction.x : target_fraction.y;
            const float  local_ratio      = std::clamp((target_axis > 0.0f) ? (placement.fraction / target_axis) : placement.fraction, 0.05f, 0.95f);
            const float  new_axis         = local_ratio * target_axis;

            ImGuiID new_node_id       = 0;
            ImGuiID remaining_node_id = 0;
            ImGui::DockBuilderSplitNode(target_node_id, direction, local_ratio, &new_node_id, &remaining_node_id);
            for (auto& [title, node_id] : window_dock_node) {
                if (node_id == target_node_id) {
                    node_id = remaining_node_id;
                }
            }
            node_root_fraction.erase(target_node_id);
            node_root_fraction[new_node_id] = horizontal
                ? ImVec2{new_axis, target_fraction.y}
                : ImVec2{target_fraction.x, new_axis};
            node_root_fraction[remaining_node_id] = horizontal
                ? ImVec2{target_axis - new_axis, target_fraction.y}
                : ImVec2{target_fraction.x, target_axis - new_axis};
            ImGui::DockBuilderDockWindow(placement.window.c_str(), new_node_id);
            window_dock_node[placement.window] = new_node_id;
        }
    }

    ImGui::DockBuilderFinish(root_dock_id);
}

} // namespace

void install_default_layout(
    erhe::imgui::Window_imgui_host& window_imgui_host,
    App_context&                    context
)
{
    // A persisted layout (imgui .ini from a previous run) always wins; the
    // procedural default layout is only built when starting from scratch.
    const std::string& imgui_ini_path = window_imgui_host.get_imgui_ini_path();
    if (!imgui_ini_path.empty() && std::filesystem::exists(imgui_ini_path)) {
        return;
    }

    // The layout is applied over two frames:
    //  - Build frame: every window referenced by a placement that is currently
    //    hidden is temporarily shown, and the DockBuilder layout is built. The
    //    windows are then submitted this same frame while docked, which binds
    //    them (and their dock nodes) for real - docking a closed window only
    //    writes a settings entry whose empty node is merged away before the
    //    window ever binds to it, leaving it misplaced when later opened.
    //  - Restore frame: the temporarily shown windows are hidden again; their
    //    dock nodes give the space back to siblings, and reopening a window
    //    from the menu returns it to its recorded place.
    enum class Layout_state { build, restore, done };
    window_imgui_host.set_dock_layout_callback(
        [&context, state = Layout_state::build, temporarily_shown = std::vector<erhe::imgui::Imgui_window*>{}]
        (erhe::imgui::Window_imgui_host& host, const ImVec2 available_size) mutable {
            if (state == Layout_state::done) {
                return;
            }
            if (state == Layout_state::restore) {
                for (erhe::imgui::Imgui_window* window : temporarily_shown) {
                    window->hide_window();
                }
                temporarily_shown.clear();
                state = Layout_state::done;
                return;
            }

            state = Layout_state::restore;

            Default_layout_config layout_config = erhe::codegen::load_config<Default_layout_config>(c_default_layout_file_path);
            if (layout_config.placements.empty()) {
                log_startup->warn("Default layout: no placements loaded from {}", c_default_layout_file_path);
                state = Layout_state::done;
                return;
            }

            const std::string viewport_title = find_primary_viewport_title(context);
            for (Dock_placement& placement : layout_config.placements) {
                if (placement.window == c_primary_viewport_substitution) {
                    placement.window = viewport_title;
                }
                if (placement.target == c_primary_viewport_substitution) {
                    placement.target = viewport_title;
                }
                erhe::imgui::Imgui_window* window = find_window_by_title(context, placement.window);
                if ((window != nullptr) && !window->is_window_visible()) {
                    window->show_window();
                    temporarily_shown.push_back(window);
                }
            }

            build_default_layout(host.get_root_dock_id(), available_size, layout_config.placements);
        }
    );
}

} // namespace editor
