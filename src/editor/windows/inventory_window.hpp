#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include "windows/inventory_slot_payload.hpp"

#include "config/generated/inventory_slot.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace erhe            { class Item_base; }
namespace erhe::commands { class Command; }

struct Inventory_config;

namespace erhe::primitive { class Material; }

namespace editor {

class App_context;
class Brush;
class Kit_item;
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

    // Schedules the settings autosave; slot contents persist into
    // editor_settings.json through write_config (collect callback).
    void touch_settings() const;

    // Push current hotbar slot state to Hotbar
    void apply_hotbar();

    // MCP hooks (set_inventory_slot, get_inventory_slots, activate_inventory_slot) on the grid and hotbar sections.
    // Adopts a brush / material into the slot exactly as a drag-drop would (Asset_reference::adopt), or with a null
    // item clears the slot's asset references and a kit slot's kit item. False for an out-of-range index, the
    // palette section or an unsupported item type.
    auto adopt_into_slot  (Slot_drag_payload::Section section, int slot_index, const std::shared_ptr<erhe::Item_base>& item) -> bool;
    // The slot becomes a kit slot of a copy of the item (doc/editor/level_tools.md "Kit slots"); false for an invalid item.
    auto set_kit_item_slot(Slot_drag_payload::Section section, int slot_index, const Kit_item& item) -> bool;
    // The slot becomes a tool slot of the tool, or vacant for null.
    auto set_tool_slot    (Slot_drag_payload::Section section, int slot_index, Tool* tool) -> bool;
    // As a click on a grid slot or the hotbar's activation of a hotbar slot; returns why nothing was activated.
    [[nodiscard]] auto activate_slot(Slot_drag_payload::Section section, int slot_index) -> std::string;
    // Null for the palette section.
    [[nodiscard]] auto get_slots(Slot_drag_payload::Section section) const -> const std::vector<Slot_entry>*;
    // What write_config saves for the slot.
    [[nodiscard]] static auto make_config_slot(const Slot_entry& entry) -> Inventory_slot;

private:
    auto resolve_tool(const std::string& tool_name) const -> Tool*;
    [[nodiscard]] auto has_new_tool() const -> bool;
    void reset_hotbar_to_tools();
    auto render_slot(int id, Slot_entry& slot, bool is_source, bool is_target, int section, int slot_index) -> bool;
    // A kit slot's button: the shape's thumbnail, its display name until the image exists. Returns whether it was clicked.
    auto kit_slot_button(const Slot_entry& slot) -> bool;
    [[nodiscard]] auto find_slot(Slot_drag_payload::Section section, int slot_index) -> Slot_entry*;
    void on_slot_changed(Slot_drag_payload::Section section, int slot_index);
    void resolve_saved_slots(std::vector<Slot_entry>& slots, const std::vector<Inventory_slot>& saved_slots, bool hotbar);
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

    // Saved config slots: their tool, command, graph node and kit fields resolve in collect_tools(). Brush / material
    // references do not wait for it: their Asset_keys are set straight into the Slot_entry Asset_references at
    // construction and resolve lazily (resolve_slot_references), so an unresolved key survives write_config by
    // construction - a not-yet-loaded brush is never silently degraded to its bare tool by the next save.
    std::vector<Inventory_slot> m_saved_grid_slots;
    std::vector<Inventory_slot> m_saved_hotbar_slots;
    std::vector<std::string>     m_known_tools; // toolbox tool descriptions at the last save

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
