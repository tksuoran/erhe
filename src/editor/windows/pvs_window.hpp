#pragma once

#include "renderers/pvs_baker.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <memory>

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::scene {
    class Camera;
}

namespace editor {

class App_context;
class Scene_root;

// The potentially visible set window (doc/plans/pvs.md "Editor";
// doc/editor/windows.md): for the active scene, the bake region and cell
// size (Scene_settings pvs_region_min / max and pvs_cell_size, with "Fit to
// content bounds"), the bake parameters, Bake (method combo) / Cancel /
// Clear, the running bake's progress and estimate, the database's state and
// sizes, the viewport camera's cell and its PVS size, and the debug overlay
// toggle (the grid cells around the camera and the camera's query box,
// drawn by Debug_visualizations).
//
// ImGui-time work (doc/editor/coding_rules.md): Bake, Cancel and Clear are
// queued (Imgui_windows::queue) and run at the start of the next prepare
// phase; the region fields are scene settings no render phase reads.
class Pvs_window : public erhe::imgui::Imgui_window
{
public:
    Pvs_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context);

    // Implements Imgui_window
    void imgui() override;

    [[nodiscard]] auto is_overlay_enabled() const -> bool { return m_show_overlay; }

private:
    void region_imgui     (Scene_root& scene_root);
    void parameters_imgui ();
    void bake_imgui       (const std::shared_ptr<Scene_root>& scene_root);
    void database_imgui   (Scene_root& scene_root);

    App_context&      m_context;
    Pvs_bake_request  m_request{};
    bool              m_show_overlay{false};
    std::string       m_last_error;
    // The progress bar's overlay, formatted when the counts change.
    std::string       m_overlay;
    uint32_t          m_overlay_cells_done{0xffffffffu};
    uint32_t          m_overlay_cell_count{0xffffffffu};
};

// The camera of the first viewport window that shows scene_root (main
// thread), or null.
[[nodiscard]] auto find_pvs_viewport_camera(App_context& context, const Scene_root& scene_root) -> std::shared_ptr<erhe::scene::Camera>;

} // namespace editor
