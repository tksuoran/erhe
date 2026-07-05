#pragma once

#include <memory>
#include <vector>

namespace erhe {
    class Item_base;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace editor {

class App_context;
class Geometry_graph_window;
class Graph_mesh;
class Graph_texture;
class Properties;
class Scene_root;
class Texture_graph_window;

// Owns the dynamically-created *extra* instances of the Properties / Geometry
// Graph / Texture Graph windows (issue #252). The primary instances remain
// owned by Editor (persisted ini labels, present in the Window menu, always
// instance #1); the extras created here get a unique title + empty ini_label
// (so their open state is not persisted across runs, matching the extra
// viewport / per-scene hierarchy windows), and are destroyed when the user
// closes them (the window X button).
//
// Creation is deferred through Imgui_windows::queue() so open_* is safe to
// call from inside ImGui iteration (context-menu callbacks, double-click) and
// from the off-thread MCP dispatch - the queued lambda runs at flush_queue()
// (end of draw_imgui_windows, outside iteration), where registering a new
// Imgui_window is allowed. Destruction of closed instances runs from
// update_once_per_frame() (the editor tick, also outside iteration).
//
// Per the App_context construction rule, this part stores the references it
// needs and does NOT read App_context members during construction; the actual
// windows are built later, in open_*.
class Editor_windows
{
public:
    Editor_windows(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );

    // Open a new Properties window pinned to target (target may be null - the
    // window then falls back to the global selection).
    void open_properties_window(const std::shared_ptr<erhe::Item_base>& target);

    // Open a new Geometry / Texture Graph window on the given target asset
    // (may be null for an empty editor). Always a fresh instance.
    void open_geometry_graph_window(const std::shared_ptr<Graph_mesh>&    target);
    void open_texture_graph_window (const std::shared_ptr<Graph_texture>& target);

    // "Open Editor" / "Open Properties" dispatch used by the item context
    // menu and item double-click (issue #252). These accept the raw tree item
    // (a scene Node, a Scene, or a Content_library_node-wrapped asset) and
    // unwrap as needed.

    // True when the item has an editor: a Graph Mesh, a Graph Texture, or a
    // Scene. Drives whether the "Open Editor" menu entry / double-click acts.
    [[nodiscard]] static auto item_has_editor(const std::shared_ptr<erhe::Item_base>& item) -> bool;
    // Open the matching editor for the item: reuse the primary graph window
    // when it has no target, else a new instance; a Scene opens a new
    // viewport. No-op when the item has no editor.
    void open_editor_for_item(const std::shared_ptr<erhe::Item_base>& item);
    // Open a new Properties window pinned to the item (unwrapping a
    // Content_library_node to its inner asset).
    void open_properties_for_item(const std::shared_ptr<erhe::Item_base>& item);
    // Open a new viewport window bound to the given scene (deferred out of
    // ImGui iteration).
    void open_scene_viewport(const std::shared_ptr<Scene_root>& scene_root);

    // Destroys extra instances whose window was closed (X). Called once per
    // frame from the editor tick (outside ImGui iteration).
    void update_once_per_frame();

private:
    // "Open Editor" for a graph asset: retarget the primary window when it
    // has no target, else open a fresh instance (keeps the window count sane).
    void open_or_reuse_geometry_graph_window(const std::shared_ptr<Graph_mesh>&    target);
    void open_or_reuse_texture_graph_window (const std::shared_ptr<Graph_texture>& target);

    erhe::imgui::Imgui_renderer& m_imgui_renderer;
    erhe::imgui::Imgui_windows&  m_imgui_windows;
    App_context&                 m_app_context;

    // Monotonic per-type counters (never reset), so a reused window title
    // never collides with a not-yet-fully-torn-down ImGui window. Start at 1
    // (the primary instance is conceptually #1); the first extra is [2].
    int m_properties_counter    {1};
    int m_geometry_graph_counter{1};
    int m_texture_graph_counter {1};

    std::vector<std::shared_ptr<Properties>>            m_properties_windows;
    std::vector<std::shared_ptr<Geometry_graph_window>> m_geometry_graph_windows;
    std::vector<std::shared_ptr<Texture_graph_window>>  m_texture_graph_windows;
};

} // namespace editor
