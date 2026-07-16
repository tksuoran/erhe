#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include "config/generated/operation_params.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace erhe            { class Item_base; }
namespace erhe::commands { class Command; }

struct Inventory_config;
struct Inventory_slot;

namespace erhe::primitive { class Material; }

namespace editor {

class App_context;
class Brush;
class Content_library_node;
class Slot_entry;
class Tool;

class Inventory_window : public erhe::imgui::Imgui_window
{
public:
    Inventory_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context,
        const Inventory_config&      config
    );

    // erhe::imgui::Imgui_window overrides
    void imgui() override;

    // Called after all tools are registered to resolve saved tool names
    void collect_tools();

    // Write current state into Inventory_config for serialization
    void write_config(Inventory_config& config) const;

    // Push current hotbar slot state to Hotbar
    void apply_hotbar();

    // Scene-close leak watchdog whitelist: slot-held items (brushes, their
    // materials, materials) are intentionally kept alive across scene close
    // (persistent inventory; see CLAUDE.md "Scene-hosted references").
    void collect_pinned_items(std::unordered_set<const erhe::Item_base*>& out_pinned) const;

private:
    auto resolve_tool(const std::string& tool_name) const -> Tool*;
    auto render_slot(int id, Slot_entry& slot, bool is_source, bool is_target, int section, int slot_index) -> bool;
    auto find_or_create_brush_with_material(
        const std::shared_ptr<Brush>&                      original_brush,
        const std::shared_ptr<erhe::primitive::Material>&  material
    ) -> std::shared_ptr<Brush>;

    App_context&            m_context;
    std::vector<Tool*>      m_all_tools;         // All toolbox-flagged tools (palette)
    std::vector<Slot_entry> m_grid_slots;        // Main inventory grid
    std::vector<Slot_entry> m_hotbar_slots;      // Hotbar row
    int                     m_column_count;
    int                     m_row_count;
    int                     m_hotbar_slot_count;

    // Saved config slot names (resolved in collect_tools)
    struct Saved_slot_name
    {
        std::string      tool_name;
        std::string      brush_name;
        std::string      material_name;
        std::string      command_name;
        Operation_params operation_params{};
        std::string      graph_node_kind;
        std::string      graph_node_type;
        std::string      graph_node_label;
    };
    std::vector<Saved_slot_name> m_saved_grid_names;
    std::vector<Saved_slot_name> m_saved_hotbar_names;

    // Brushes / materials referenced by saved slots resolve by name against
    // the scene content libraries. The libraries may not be populated yet at
    // collect_tools() time (scenes can load asynchronously), so unresolved
    // names are retried each frame the window renders, and write_config()
    // preserves them verbatim until they resolve - a not-yet-loaded brush
    // must not be silently degraded to its bare tool by the next save.
    struct Pending_slot_item
    {
        bool        hotbar{false}; // grid slot otherwise
        int         index {-1};
        std::string brush_name;
        std::string material_name;
    };
    std::vector<Pending_slot_item> m_pending_slot_items;

    // Assigns resolved items into their slots and drops completed entries;
    // returns true when a hotbar slot changed (caller re-applies the hotbar).
    auto resolve_pending_slot_items() -> bool;
    // Forgets an unresolved name once the user clears / overwrites its slot.
    void drop_pending_slot_item(bool hotbar, int index);
    [[nodiscard]] auto find_pending_slot_item(bool hotbar, int index) const -> const Pending_slot_item*;
    [[nodiscard]] auto find_brush_by_name   (const std::string& name) const -> std::shared_ptr<Brush>;
    [[nodiscard]] auto find_material_by_name(const std::string& name) const -> std::shared_ptr<erhe::primitive::Material>;
};

}
