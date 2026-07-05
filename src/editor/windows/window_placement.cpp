#include "windows/window_placement.hpp"

#include "geometry_graph/geometry_graph_window.hpp"
#include "texture_graph/texture_graph_window.hpp"
#include "windows/properties.hpp"
#include "windows/viewport_window.hpp"

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <string>
#include <vector>

namespace editor {

namespace {

// New editor windows cover 66% of the viewport; new Properties windows are
// narrower (a third of the width) but the same height.
constexpr float c_editor_window_ratio     = 0.66f;
constexpr float c_properties_width_ratio  = 0.33f;
constexpr float c_properties_height_ratio = 0.66f;

auto is_editor_window(const erhe::imgui::Imgui_window* window) -> bool
{
    return (dynamic_cast<const Viewport_window*>      (window) != nullptr)
        || (dynamic_cast<const Geometry_graph_window*>(window) != nullptr)
        || (dynamic_cast<const Texture_graph_window*> (window) != nullptr);
}

// Title of the best editor window to dock the new window with: first a visible
// window of the same concrete type as new_window, then any visible editor
// window. Excludes new_window itself. Empty when there is no candidate.
auto find_editor_dock_target(
    erhe::imgui::Imgui_windows&      imgui_windows,
    const erhe::imgui::Imgui_window& new_window
) -> std::string
{
    const std::vector<erhe::imgui::Imgui_window*>& windows = imgui_windows.get_windows();

    // Exact concrete-type match takes priority.
    for (const erhe::imgui::Imgui_window* window : windows) {
        if ((window == &new_window) || !window->is_window_visible()) {
            continue;
        }
        const bool same_type =
            ((dynamic_cast<const Viewport_window*>      (&new_window) != nullptr) && (dynamic_cast<const Viewport_window*>      (window) != nullptr)) ||
            ((dynamic_cast<const Geometry_graph_window*>(&new_window) != nullptr) && (dynamic_cast<const Geometry_graph_window*>(window) != nullptr)) ||
            ((dynamic_cast<const Texture_graph_window*> (&new_window) != nullptr) && (dynamic_cast<const Texture_graph_window*> (window) != nullptr));
        if (same_type) {
            return window->get_title();
        }
    }

    // Otherwise any editor window.
    for (const erhe::imgui::Imgui_window* window : windows) {
        if ((window == &new_window) || !window->is_window_visible()) {
            continue;
        }
        if (is_editor_window(window)) {
            return window->get_title();
        }
    }

    return {};
}

} // anonymous namespace

void apply_editor_window_placement(erhe::imgui::Imgui_windows& imgui_windows, erhe::imgui::Imgui_window& new_window)
{
    const std::string target = find_editor_dock_target(imgui_windows, new_window);
    new_window.set_initial_placement(target, c_editor_window_ratio, c_editor_window_ratio);
}

void apply_properties_window_placement(erhe::imgui::Imgui_windows& imgui_windows, erhe::imgui::Imgui_window& new_window)
{
    std::string target;
    const std::vector<erhe::imgui::Imgui_window*>& windows = imgui_windows.get_windows();
    for (const erhe::imgui::Imgui_window* window : windows) {
        if ((window == &new_window) || !window->is_window_visible()) {
            continue;
        }
        if (dynamic_cast<const Properties*>(window) != nullptr) {
            target = window->get_title();
            break;
        }
    }
    new_window.set_initial_placement(target, c_properties_width_ratio, c_properties_height_ratio);
}

} // namespace editor
