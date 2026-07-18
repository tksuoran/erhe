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

    // MCP hook (set_inventory_slot): adopt a brush / material into a grid
    // slot exactly as a drag-drop would (Asset_reference::adopt), or clear
    // the slot's asset references when item is null. Returns false for an
    // out-of-range index or an unsupported item type.
    auto adopt_into_grid_slot(int slot_index, const std::shared_ptr<erhe::Item_base>& item) -> bool;

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

    // Saved config slot names (resolved in collect_tools). Brush / material
    // references are NOT here: their Asset_keys are set straight into the
    // Slot_entry Asset_references at construction and resolve lazily
    // (resolve_slot_references), so an unresolved key survives write_config
    // by construction - a not-yet-loaded brush is never silently degraded
    // to its bare tool by the next save.
    struct Saved_slot_name
    {
        std::string      tool_name;
        std::string      command_name;
        Operation_params operation_params{};
        std::string      graph_node_kind;
        std::string      graph_node_type;
        std::string      graph_node_label;
    };
    std::vector<Saved_slot_name> m_saved_grid_names;
    std::vector<Saved_slot_name> m_saved_hotbar_names;

    // Per-frame resolve of slot references still carrying an unresolved key
    // (their content library was not loaded yet - scenes load
    // asynchronously; scene_local misses do not latch). Returns true when a
    // hotbar slot resolved (caller re-applies the hotbar).
    auto resolve_slot_references() -> bool;
    // (Re)stamps the slot's asset-usership labels ("inventory grid slot N" /
    // "inventory hotbar slot N") so unload refusals and query_asset_manager
    // name the holding slot; applied after any slot content change (labels
    // travel with Slot_entry copies in swaps).
    static void set_slot_labels(Slot_entry& slot, bool hotbar, int index);
};

}
