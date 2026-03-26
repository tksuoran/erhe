#include "windows/inventory_window.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "scene/scene_root.hpp"
#include "graphics/icon_set.hpp"
#include "graphics/thumbnails.hpp"
#include "preview/brush_preview.hpp"
#include "preview/material_preview.hpp"
#include "erhe_primitive/material.hpp"
#include "tools/hotbar.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"

#include "config/generated/inventory_slot.hpp"
#include "config/generated/inventory_config.hpp"

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <imgui/imgui.h>

namespace editor {

namespace {

constexpr float c_slot_size = 48.0f;

enum class Slot_section : int { palette = 0, grid = 1, hotbar = 2 };

// POD payload for drag-and-drop (safe for memcpy by ImGui)
class Slot_drag_payload
{
public:
    Tool*                      tool    {nullptr};
    Brush*                     brush   {nullptr};
    erhe::primitive::Material* material{nullptr};
    Slot_section               section {Slot_section::palette};
    int                        index   {-1};
};

} // anonymous namespace

Inventory_window::Inventory_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context,
    const Inventory_config&      config
)
    : Imgui_window      {imgui_renderer, imgui_windows, "Inventory", "inventory"}
    , m_context          {app_context}
    , m_column_count     {config.column_count}
    , m_row_count        {config.row_count}
    , m_hotbar_slot_count{config.hotbar_slot_count}
{
    const int grid_count = m_column_count * m_row_count;
    m_grid_slots.resize(grid_count);
    m_hotbar_slots.resize(m_hotbar_slot_count);

    // Save slot names for later resolution (after tools are registered)
    m_saved_grid_names.reserve(config.grid_slots.size());
    for (const Inventory_slot& slot : config.grid_slots) {
        m_saved_grid_names.push_back(Saved_slot_name{slot.tool_name, slot.brush_name, slot.material_name});
    }
    m_saved_hotbar_names.reserve(config.hotbar_slots.size());
    for (const Inventory_slot& slot : config.hotbar_slots) {
        m_saved_hotbar_names.push_back(Saved_slot_name{slot.tool_name, slot.brush_name, slot.material_name});
    }
}

void Inventory_window::collect_tools()
{
    m_all_tools.clear();
    const std::vector<Tool*>& tools = m_context.tools->get_tools();
    for (Tool* tool : tools) {
        const char* icon = tool->get_icon();
        if (icon == nullptr) {
            continue;
        }
        if (erhe::utility::test_bit_set(tool->get_flags(), Tool_flags::toolbox)) {
            m_all_tools.push_back(tool);
        }
    }

    // Resolve saved grid slot names
    const int grid_count = m_column_count * m_row_count;
    for (int i = 0; i < grid_count; ++i) {
        if (i < static_cast<int>(m_saved_grid_names.size())) {
            const Saved_slot_name& saved = m_saved_grid_names[i];
            m_grid_slots[i].tool  = resolve_tool(saved.tool_name);
            // Brush resolution would require content library access - deferred for now
            // Brushes are resolved by name when the content library is available
        }
    }
    m_saved_grid_names.clear();

    // Resolve saved hotbar slot names
    for (int i = 0; i < m_hotbar_slot_count; ++i) {
        if (i < static_cast<int>(m_saved_hotbar_names.size())) {
            const Saved_slot_name& saved = m_saved_hotbar_names[i];
            m_hotbar_slots[i].tool = resolve_tool(saved.tool_name);
        }
    }
    m_saved_hotbar_names.clear();

    // If hotbar has no configured slots, populate with all tools (first-run default)
    bool hotbar_empty = true;
    for (const Slot_entry& entry : m_hotbar_slots) {
        if ((entry.tool != nullptr) || entry.brush || entry.material) {
            hotbar_empty = false;
            break;
        }
    }
    if (hotbar_empty) {
        for (int i = 0; i < m_hotbar_slot_count && i < static_cast<int>(m_all_tools.size()); ++i) {
            m_hotbar_slots[i].tool = m_all_tools[i];
        }
    }

    apply_hotbar();
}

auto Inventory_window::resolve_tool(const std::string& tool_name) const -> Tool*
{
    if (tool_name.empty()) {
        return nullptr;
    }
    for (Tool* tool : m_all_tools) {
        if (tool->get_description() == tool_name) {
            return tool;
        }
    }
    return nullptr;
}

auto Inventory_window::render_slot(const int id, Slot_entry& slot, const bool is_source, const bool is_target, const int section, const int slot_index) -> bool
{
    bool changed  = false;
    bool has_item = (slot.tool != nullptr) || slot.brush || slot.material;

    const ImVec2 button_size{c_slot_size, c_slot_size};

    // Background color
    const ImVec4 bg_color = has_item
        ? ImVec4{0.2f, 0.2f, 0.5f, 0.8f}
        : ImVec4{0.1f, 0.1f, 0.15f, 0.6f};

    ImGui::PushID(id);

    bool thumbnail_drawn = false;

    // Brush slot: render thumbnail
    if (slot.brush && m_context.thumbnails && m_context.brush_preview) {
        std::shared_ptr<Brush> brush = slot.brush;
        thumbnail_drawn = m_context.thumbnails->draw(
            brush,
            [this, brush](const std::shared_ptr<erhe::graphics::Texture>& texture, int64_t time) {
                m_context.brush_preview->render_preview(texture, brush, time);
            },
            c_slot_size
        );
        if (thumbnail_drawn && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", brush->get_name().c_str());
        }
    }

    // Material slot: render thumbnail
    if (!thumbnail_drawn && slot.material && m_context.thumbnails && m_context.material_preview) {
        std::shared_ptr<erhe::primitive::Material> material = slot.material;
        thumbnail_drawn = m_context.thumbnails->draw(
            material,
            [this, material](const std::shared_ptr<erhe::graphics::Texture>& texture, int64_t) {
                m_context.material_preview->render_preview(texture, material);
            },
            c_slot_size
        );
        if (thumbnail_drawn && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", material->get_name().c_str());
        }
    }

    if (!thumbnail_drawn) {
        // Tool slot or empty: render icon button
        ImGui::PushStyleColor(ImGuiCol_Button,        bg_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4{0.3f, 0.3f, 0.6f, 0.9f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4{0.4f, 0.4f, 0.7f, 1.0f});

        if (slot.tool != nullptr) {
            const char*  icon      = slot.tool->get_icon();
            ImFont*      icon_font = slot.tool->get_icon_font();
            const float  font_size =
                m_context.imgui_renderer->get_imgui_settings().scale_factor *
                m_context.imgui_renderer->get_imgui_settings().icon_font_size;

            if ((icon != nullptr) && (icon_font != nullptr)) {
                ImGui::PushFont(icon_font, font_size);
                ImGui::Button(icon, button_size);
                ImGui::PopFont();
            } else {
                ImGui::Button("?", button_size);
            }
        } else {
            ImGui::Button("##empty", button_size);
        }

        ImGui::PopStyleColor(3);

        // Tooltip for tools
        if ((slot.tool != nullptr) && !slot.brush && !slot.material && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", slot.tool->get_description());
        }
    }

    // Drag source
    if (is_source && has_item) {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            Slot_drag_payload drag{
                .tool     = slot.tool,
                .brush    = slot.brush.get(),
                .material = slot.material.get(),
                .section  = static_cast<Slot_section>(section),
                .index    = slot_index
            };
            ImGui::SetDragDropPayload("Inventory_Slot", &drag, sizeof(Slot_drag_payload));
            if (slot.brush) {
                ImGui::Text("%s", slot.brush->get_name().c_str());
            } else if (slot.material) {
                ImGui::Text("%s", slot.material->get_name().c_str());
            } else if (slot.tool != nullptr) {
                ImGui::Text("%s", slot.tool->get_description());
            }
            ImGui::EndDragDropSource();
        }
    }

    // Right-click to clear
    if (is_target && has_item && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        slot.tool = nullptr;
        slot.brush.reset();
        slot.material.reset();
        changed = true;
    }

    // Drop target
    if (is_target) {
        if (ImGui::BeginDragDropTarget()) {
            // Accept inventory slot drops (swap)
            const ImGuiPayload* slot_payload = ImGui::AcceptDragDropPayload("Inventory_Slot");
            if (slot_payload != nullptr) {
                const Slot_drag_payload& source = *static_cast<const Slot_drag_payload*>(slot_payload->Data);

                // Reconstruct source entry from payload
                Slot_entry source_entry;
                source_entry.tool = source.tool;
                if (source.brush != nullptr) {
                    source_entry.brush = std::dynamic_pointer_cast<Brush>(source.brush->shared_from_this());
                }
                if (source.material != nullptr) {
                    source_entry.material = std::dynamic_pointer_cast<erhe::primitive::Material>(source.material->shared_from_this());
                }

                // Find the source slot and swap (palette sources are copy-only)
                Slot_entry* source_slot = nullptr;
                if (source.section == Slot_section::grid) {
                    if ((source.index >= 0) && (source.index < static_cast<int>(m_grid_slots.size()))) {
                        source_slot = &m_grid_slots[source.index];
                    }
                } else if (source.section == Slot_section::hotbar) {
                    if ((source.index >= 0) && (source.index < static_cast<int>(m_hotbar_slots.size()))) {
                        source_slot = &m_hotbar_slots[source.index];
                    }
                }

                if (source_slot != nullptr) {
                    // Swap: source gets old dest contents, dest gets source contents
                    Slot_entry old_dest = slot;
                    slot         = source_entry;
                    *source_slot = old_dest;
                } else {
                    // Palette: copy only
                    slot = source_entry;
                }
                changed = true;
            }

            // Accept content library node drops (brushes and materials)
            const ImGuiPayload* node_payload = ImGui::AcceptDragDropPayload("Content_library_node");
            if (node_payload != nullptr) {
                erhe::Item_base* item_base = *static_cast<erhe::Item_base**>(node_payload->Data);
                Content_library_node* node = dynamic_cast<Content_library_node*>(item_base);
                if (node != nullptr) {
                    std::shared_ptr<Brush> dropped_brush = std::dynamic_pointer_cast<Brush>(node->item);
                    if (dropped_brush) {
                        slot.brush    = dropped_brush;
                        slot.material.reset();
                        slot.tool     = m_context.brush_tool;
                        changed       = true;
                    }
                    std::shared_ptr<erhe::primitive::Material> dropped_material =
                        std::dynamic_pointer_cast<erhe::primitive::Material>(node->item);
                    if (dropped_material) {
                        if (slot.brush) {
                            // Slot has a brush: find or create a forked brush with this material
                            slot.brush = find_or_create_brush_with_material(slot.brush, dropped_material);
                        } else {
                            // Empty or tool slot: make it a material slot
                            slot.material = dropped_material;
                            slot.brush.reset();
                            slot.tool     = m_context.material_paint_tool;
                        }
                        changed = true;
                    }
                }
            }

            ImGui::EndDragDropTarget();
        }
    }

    ImGui::PopID();

    return changed;
}

auto Inventory_window::find_or_create_brush_with_material(
    const std::shared_ptr<Brush>&                     original_brush,
    const std::shared_ptr<erhe::primitive::Material>& material
) -> std::shared_ptr<Brush>
{
    const std::shared_ptr<erhe::geometry::Geometry> original_geometry = original_brush->get_geometry();

    // Search all content libraries for the original brush and a matching fork
    for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
        const std::shared_ptr<Content_library>& content_library = scene_root->get_content_library();
        if (!content_library || !content_library->brushes) {
            continue;
        }
        Content_library_node& brushes_node = *content_library->brushes;

        bool contains_original = false;
        std::shared_ptr<Brush> existing_fork;
        const std::vector<std::shared_ptr<Brush>>& all_brushes = brushes_node.get_all<Brush>();
        for (const std::shared_ptr<Brush>& b : all_brushes) {
            if (b.get() == original_brush.get()) {
                contains_original = true;
            }
            // Match by same geometry and same material pointer
            if ((b->get_geometry() == original_geometry) && (b->get_material() == material)) {
                existing_fork = b;
            }
        }

        if (existing_fork) {
            return existing_fork;
        }

        if (contains_original) {
            std::shared_ptr<Brush> forked = original_brush->make_with_material(material);
            brushes_node.add(forked);
            return forked;
        }
    }

    // Fallback: original not found in any content library, create unregistered fork
    return original_brush->make_with_material(material);
}

void Inventory_window::imgui()
{
    int next_id = 0;

    // --- Tools Palette (top) ---
    ImGui::TextUnformatted("Tools");
    ImGui::Separator();

    const int palette_columns = m_column_count;
    if (ImGui::BeginTable("##palette", palette_columns, ImGuiTableFlags_None)) {
        for (int i = 0; i < static_cast<int>(m_all_tools.size()); ++i) {
            ImGui::TableNextColumn();
            Slot_entry palette_entry{.tool = m_all_tools[i]};
            render_slot(++next_id, palette_entry, true, false, 0, i);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();

    // --- Main Inventory Grid (middle) ---
    ImGui::TextUnformatted("Inventory");
    ImGui::Separator();

    bool grid_changed = false;
    if (ImGui::BeginTable("##grid", m_column_count, ImGuiTableFlags_None)) {
        const int grid_count = m_column_count * m_row_count;
        for (int i = 0; i < grid_count; ++i) {
            ImGui::TableNextColumn();
            if (render_slot(++next_id, m_grid_slots[i], true, true, 1, i)) {
                grid_changed = true;
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();

    // --- Hotbar (bottom) ---
    ImGui::TextUnformatted("Hotbar");
    ImGui::Separator();

    bool any_changed = false;
    if (ImGui::BeginTable("##hotbar", m_hotbar_slot_count, ImGuiTableFlags_None)) {
        for (int i = 0; i < m_hotbar_slot_count; ++i) {
            ImGui::TableNextColumn();
            if (render_slot(++next_id, m_hotbar_slots[i], true, true, 2, i)) {
                any_changed = true;
            }
        }
        ImGui::EndTable();
    }

    // Any change (including swaps involving hotbar slots from grid drops) may
    // affect the hotbar. apply_hotbar() is cheap (sets a dirty flag), so always call.
    if (any_changed || grid_changed) {
        apply_hotbar();
    }
}

void Inventory_window::apply_hotbar()
{
    if (m_context.hotbar == nullptr) {
        return;
    }
    std::vector<Slot_entry> filtered;
    filtered.reserve(m_hotbar_slots.size());
    for (const Slot_entry& entry : m_hotbar_slots) {
        if ((entry.tool != nullptr) || entry.brush || entry.material) {
            filtered.push_back(entry);
        }
    }
    m_context.hotbar->set_slots(filtered);
}

void Inventory_window::write_config(Inventory_config& config) const
{
    config.column_count      = m_column_count;
    config.row_count         = m_row_count;
    config.hotbar_slot_count = m_hotbar_slot_count;

    config.grid_slots.clear();
    for (const Slot_entry& entry : m_grid_slots) {
        Inventory_slot slot;
        slot.tool_name     = (entry.tool != nullptr) ? entry.tool->get_description() : "";
        slot.brush_name    = entry.brush    ? entry.brush->get_name()    : "";
        slot.material_name = entry.material ? entry.material->get_name() : "";
        config.grid_slots.push_back(slot);
    }

    config.hotbar_slots.clear();
    for (const Slot_entry& entry : m_hotbar_slots) {
        Inventory_slot slot;
        slot.tool_name     = (entry.tool != nullptr) ? entry.tool->get_description() : "";
        slot.brush_name    = entry.brush    ? entry.brush->get_name()    : "";
        slot.material_name = entry.material ? entry.material->get_name() : "";
        config.hotbar_slots.push_back(slot);
    }
}

}
