#include "editor_default_layout.hpp"

#include "app_context.hpp"
#include "windows/viewport_window.hpp"

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/window_imgui_host.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <string>
#include <vector>

namespace editor {

namespace {

constexpr float c_default_bottom_ratio = 0.25f;
constexpr float c_max_bottom_ratio     = 0.5f;

struct Layout_spec
{
    std::string center_window_title;
    std::string bottom_window_title;
};

enum class Layout_state
{
    Initial,
    Measuring,
    Done
};

// Best-effort lookup of the primary viewport window title. The Viewport_window's
// ImGui title is composed as "{name}##{index}" by Scene_views::create_viewport_window,
// so the first viewport window's title carries "##0".
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

void build_default_layout(
    const ImGuiID      root_dock_id,
    const ImVec2       available_size,
    const Layout_spec& spec
)
{
    // Determine the bottom strip size from the measured height of the bottom window.
    float bottom_size = c_default_bottom_ratio * available_size.y;
    if (!spec.bottom_window_title.empty()) {
        const ImGuiWindow* measured = ImGui::FindWindowByName(spec.bottom_window_title.c_str());
        if (measured != nullptr && measured->SizeFull.y > 0.0f) {
            bottom_size = std::min(measured->SizeFull.y, c_max_bottom_ratio * available_size.y);
        }
    }
    const float bottom_ratio = (available_size.y > 0.0f)
        ? (bottom_size / available_size.y)
        : c_default_bottom_ratio;

    ImGuiID bottom_id = 0;
    ImGuiID center_id = 0;
    ImGui::DockBuilderSplitNode(root_dock_id, ImGuiDir_Down, bottom_ratio, &bottom_id, &center_id);
    ImGui::DockBuilderSetNodeSize(bottom_id, ImVec2{available_size.x, bottom_size});

    if (!spec.center_window_title.empty()) {
        ImGui::DockBuilderDockWindow(spec.center_window_title.c_str(), center_id);
    }
    if (!spec.bottom_window_title.empty()) {
        ImGui::DockBuilderDockWindow(spec.bottom_window_title.c_str(), bottom_id);
    }

    ImGui::DockBuilderFinish(root_dock_id);
}

} // namespace

void install_default_layout(
    erhe::imgui::Window_imgui_host& window_imgui_host,
    App_context&                    context
)
{
    window_imgui_host.set_dock_layout_callback(
        [&context, state = Layout_state::Initial, spec = Layout_spec{}]
        (erhe::imgui::Window_imgui_host& host, const ImVec2 available_size) mutable {
            if (state == Layout_state::Done) {
                return;
            }

            const ImGuiID root_dock_id = host.get_root_dock_id();

            if (state == Layout_state::Initial) {
                // If the dockspace already has a split layout (loaded from imgui.ini),
                // leave the user's arrangement alone.
                const ImGuiDockNode* node = ImGui::DockBuilderGetNode(root_dock_id);
                if (node != nullptr && node->IsSplitNode()) {
                    state = Layout_state::Done;
                    return;
                }

                // Fresh dockspace: capture titles now and let ImGui spend one frame
                // sizing the windows to their content. DockSpace() will create the
                // root node below if it doesn't exist yet.
                spec.center_window_title = find_primary_viewport_title(context);
                spec.bottom_window_title = "Animation";
                state = Layout_state::Measuring;
                return;
            }

            // Layout_state::Measuring - windows have been sized by ImGui on the
            // previous frame; now build the split layout and dock them.
            build_default_layout(root_dock_id, available_size, spec);
            state = Layout_state::Done;
        }
    );
}

} // namespace editor
