#pragma once

#include "app_message.hpp"
#include "config/generated/lod_config.hpp"

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_primitive/lod.hpp"
#include "erhe_property/expression.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe {
    class Item_base;
}
namespace erhe {
    class Item_host;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::primitive {
    class Buffer_mesh;
    class Primitive;
}
namespace erhe::scene {
    class Mesh;
}
namespace erhe::scene_renderer {
    class Draw_list_entry;
}

namespace editor {

class App_context;
class App_message_bus;
class Scene_root;

// The chain level whose index range a draw-list entry draws, matched by its
// range (the entry holds the copy registration made). -1 when no level
// matches (the entry was registered from another variant or from a buffer
// mesh published since). Shared by the LOD window and the MCP get_lod_info.
[[nodiscard]] auto find_draw_list_entry_lod_level(
    const erhe::scene_renderer::Draw_list_entry& entry,
    const erhe::primitive::Buffer_mesh&          buffer_mesh
) -> int;

// The LOD window (doc/editor/lod.md, doc/plans/lod.md step 3): generation
// settings (defaults from the editor config section `lod`), Generate / Clear
// for the selected meshes of the active scene (request_lod_generation()), a
// table per distinct primitive of those meshes (level, triangles, ratio to
// level 0, object space error, pixel error in the last active viewport, the
// level the mesh draws), and a level slider that forces a level on the
// selected meshes through the Lod properties (undoable).
//
// ImGui-time work (doc/editor/coding_rules.md): Generate and Clear are queued
// (Imgui_windows::queue); the slider and Release queue property operations
// on the operation stack. The slider writes the values live while it is
// dragged, as the Properties window rows do, and records one compound
// operation when the edit ends.
class Lod_window : public erhe::imgui::Imgui_window
{
public:
    Lod_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context,
        App_message_bus&             app_message_bus,
        const Lod_config&            defaults
    );

    // Implements Imgui_window
    void imgui() override;

private:
    class Primitive_row
    {
    public:
        const erhe::primitive::Primitive* primitive{nullptr};
        const erhe::scene::Mesh*          mesh{nullptr}; // first selected mesh using the primitive
        std::size_t                       mesh_primitive_index{0};
    };

    // One mesh of a slider edit and its local states from before the edit.
    class Edit_entry
    {
    public:
        std::shared_ptr<erhe::scene::Mesh>         mesh;
        std::optional<erhe::property::Local_state> apply_lod;
        std::optional<erhe::property::Local_state> mode;
        std::optional<erhe::property::Local_state> forced_level;
    };

    void collect_selection(const Scene_root& scene_root);
    void settings_imgui   ();
    void generate_imgui   (const std::shared_ptr<Scene_root>& scene_root);
    void level_imgui      ();
    void table_imgui      (Scene_root& scene_root);
    void write_forced_level(int level);
    void begin_level_edit ();
    void end_level_edit   ();
    void release_selection();
    void drop_dead_edit_entries();
    void on_close_scene  (const erhe::Item_host* closing_host);
    void on_items_removed(const Removed_items& removed);

    App_context&                  m_context;
    erhe::primitive::Lod_settings m_settings{};

    // Per draw scratch (capacity kept, cleared at the start and end of each
    // draw so no reference outlives it).
    std::vector<std::shared_ptr<erhe::scene::Mesh>> m_meshes;
    std::vector<Primitive_row>                      m_rows;

    // Slider edit session. The entries hold their meshes for the length of
    // a drag only; a closing scene or a removal drops them
    // (doc/editor/coding_rules.md "Scene-hosted references in editor parts").
    int                     m_level{0};
    bool                    m_level_editing{false};
    std::vector<Edit_entry> m_edit_entries;

    erhe::message_bus::Subscription<Close_scene_message>   m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;

    // Generation request state.
    bool        m_request_queued{false};
    std::string m_last_request;
};

} // namespace editor
