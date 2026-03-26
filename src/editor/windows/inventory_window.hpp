#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <string>
#include <vector>

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
        std::string tool_name;
        std::string brush_name;
        std::string material_name;
    };
    std::vector<Saved_slot_name> m_saved_grid_names;
    std::vector<Saved_slot_name> m_saved_hotbar_names;
};

}
