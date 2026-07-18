#include "windows/inventory_window.hpp"
#include "windows/inventory_slot_payload.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "assets/asset_manager.hpp"
#include "assets/asset_reference_config.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "scene/scene_root.hpp"
#include "graph_editor/graph_editor_window_base.hpp"
#include "graph_editor/graph_node_drag_payload.hpp"
#include "graphics/icon_set.hpp"
#include "graphics/thumbnails.hpp"
#include "preview/brush_preview.hpp"
#include "preview/material_preview.hpp"
#include "erhe_primitive/material.hpp"
#include "operations/operation_drag_payload.hpp"
#include "operations/operations_window.hpp"
#include "tools/hotbar.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"

#include "config/generated/inventory_slot.hpp"
#include "config/generated/inventory_config.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <fmt/format.h>

#include <imgui/imgui.h>

#include <cstdio>

namespace editor {

namespace {

constexpr float c_slot_size = 48.0f;

using Slot_section = Slot_drag_payload::Section;

// Asset key of a saved slot reference: the v4 brush_asset / material_asset
// key when present, else the legacy pre-v4 name migrated to a scene_local
// key (same semantics the name-based lookup had).
[[nodiscard]] auto slot_asset_key(const Asset_reference_data& asset, const std::string& legacy_name, const Asset_type type) -> Asset_key
{
    Asset_key key = to_asset_key(asset);
    if (key.is_empty() && !legacy_name.empty()) {
        key = Asset_key{.scope = Asset_scope::scene_local, .type = type, .name = legacy_name};
    }
    return key;
}

} // anonymous namespace

void Inventory_window::set_slot_labels(Slot_entry& slot, const bool hotbar, const int index)
{
    const std::string label = hotbar
        ? fmt::format("inventory hotbar slot {}", index + 1)
        : fmt::format("inventory grid slot {}", index + 1);
    slot.brush   .set_user_label(label);
    slot.material.set_user_label(label);
}

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

    // Save tool / command slot names for later resolution (after tools are
    // registered). Brush / material references need no deferred pass: their
    // keys go straight into the Slot_entry Asset_references here and resolve
    // lazily (resolve_slot_references).
    m_saved_grid_names.reserve(config.grid_slots.size());
    for (const Inventory_slot& slot : config.grid_slots) {
        m_saved_grid_names.push_back(Saved_slot_name{slot.tool_name, slot.command_name, slot.operation_params, slot.graph_node_kind, slot.graph_node_type, slot.graph_node_label});
    }
    m_saved_hotbar_names.reserve(config.hotbar_slots.size());
    for (const Inventory_slot& slot : config.hotbar_slots) {
        m_saved_hotbar_names.push_back(Saved_slot_name{slot.tool_name, slot.command_name, slot.operation_params, slot.graph_node_kind, slot.graph_node_type, slot.graph_node_label});
    }

    const auto load_slot_references = [](std::vector<Slot_entry>& slots, const std::vector<Inventory_slot>& saved_slots, const bool hotbar) {
        for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
            Slot_entry& entry = slots[i];
            if (i < static_cast<int>(saved_slots.size())) {
                const Inventory_slot& saved = saved_slots[i];
                entry.brush   .set_key(slot_asset_key(saved.brush_asset,    saved.brush_name,    Asset_type::brush));
                entry.material.set_key(slot_asset_key(saved.material_asset, saved.material_name, Asset_type::material));
            }
            set_slot_labels(entry, hotbar, i);
        }
    };
    load_slot_references(m_grid_slots,   config.grid_slots,   false);
    load_slot_references(m_hotbar_slots, config.hotbar_slots, true);
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
            if (!saved.command_name.empty() && (m_context.commands != nullptr)) {
                m_grid_slots[i].command          = m_context.commands->find_command(saved.command_name);
                m_grid_slots[i].operation_params = saved.operation_params;
            }
            // Graph-node slots are plain strings; no resolution needed.
            m_grid_slots[i].graph_node_kind  = saved.graph_node_kind;
            m_grid_slots[i].graph_node_type  = saved.graph_node_type;
            m_grid_slots[i].graph_node_label = saved.graph_node_label;
        }
    }
    m_saved_grid_names.clear();

    // Resolve saved hotbar slot names
    for (int i = 0; i < m_hotbar_slot_count; ++i) {
        if (i < static_cast<int>(m_saved_hotbar_names.size())) {
            const Saved_slot_name& saved = m_saved_hotbar_names[i];
            m_hotbar_slots[i].tool = resolve_tool(saved.tool_name);
            if (!saved.command_name.empty() && (m_context.commands != nullptr)) {
                m_hotbar_slots[i].command          = m_context.commands->find_command(saved.command_name);
                m_hotbar_slots[i].operation_params = saved.operation_params;
            }
            m_hotbar_slots[i].graph_node_kind  = saved.graph_node_kind;
            m_hotbar_slots[i].graph_node_type  = saved.graph_node_type;
            m_hotbar_slots[i].graph_node_label = saved.graph_node_label;
        }
    }
    m_saved_hotbar_names.clear();

    // The default scene's content library normally exists by now, so saved
    // brush / material references resolve here; scenes still loading resolve
    // in imgui(). apply_hotbar() below picks up any resolved hotbar slots.
    resolve_slot_references();

    // If hotbar has no configured slots, populate with all tools (first-run default).
    // An operation (command) slot counts as configured too, so a hotbar filled only
    // with operations is not overwritten by the default tools on reload. A brush /
    // material reference counts even while unresolved (non-empty key).
    bool hotbar_empty = true;
    for (const Slot_entry& entry : m_hotbar_slots) {
        if (
            (entry.tool != nullptr)              ||
            !entry.brush.get_key().is_empty()    ||
            !entry.material.get_key().is_empty() ||
            (entry.command != nullptr)           ||
            !entry.graph_node_type.empty()
        ) {
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

auto Inventory_window::resolve_slot_references() -> bool
{
    Asset_manager* const asset_manager = m_context.asset_manager;
    if (asset_manager == nullptr) {
        return false;
    }
    bool hotbar_changed = false;
    const auto resolve_slots = [asset_manager, &hotbar_changed](std::vector<Slot_entry>& slots, const bool hotbar) {
        for (Slot_entry& slot : slots) {
            for (Asset_reference* const reference : { &slot.brush, &slot.material }) {
                if ((reference->get_state() != Asset_resolve_state::unresolved) || reference->get_key().is_empty()) {
                    continue;
                }
                if (reference->resolve(*asset_manager)) {
                    hotbar_changed = hotbar_changed || hotbar;
                }
            }
        }
    };
    resolve_slots(m_grid_slots,   false);
    resolve_slots(m_hotbar_slots, true);
    return hotbar_changed;
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
    // Presence means a RESOLVED reference (parity with the legacy behavior
    // for a not-yet-loaded name): an unresolved key renders as vacant but
    // still persists through write_config.
    const std::shared_ptr<Brush>                     slot_brush    = slot.get_brush();
    const std::shared_ptr<erhe::primitive::Material> slot_material = slot.get_material();

    bool changed  = false;
    bool has_item = (slot.tool != nullptr) || slot_brush || slot_material || (slot.command != nullptr) || !slot.graph_node_type.empty();

    const ImVec2 button_size{c_slot_size, c_slot_size};

    // Background color
    const ImVec4 bg_color = has_item
        ? ImVec4{0.2f, 0.2f, 0.5f, 0.8f}
        : ImVec4{0.1f, 0.1f, 0.15f, 0.6f};

    ImGui::PushID(id);

    bool thumbnail_drawn = false;

    // Brush slot: render thumbnail
    if (slot_brush && m_context.thumbnails && m_context.brush_preview) {
        std::shared_ptr<Brush> brush = slot_brush;
        thumbnail_drawn = m_context.thumbnails->draw(
            brush,
            [&context = m_context, brush](const std::shared_ptr<erhe::graphics::Texture>& texture, unsigned int texture_layer, int64_t time) {
                context.brush_preview->render_preview(texture, texture_layer, brush, time);
            },
            c_slot_size
        );
        if (thumbnail_drawn && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", brush->get_name().c_str());
        }
    }

    // Material slot: render thumbnail
    if (!thumbnail_drawn && slot_material && m_context.thumbnails && m_context.material_preview) {
        std::shared_ptr<erhe::primitive::Material> material = slot_material;
        thumbnail_drawn = m_context.thumbnails->draw(
            material,
            [&context = m_context, material](const std::shared_ptr<erhe::graphics::Texture>& texture, unsigned int /*texture_layer*/, int64_t) {
                context.material_preview->render_preview(texture, material);
            },
            c_slot_size
        );
        if (thumbnail_drawn && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", material->get_name().c_str());
        }
    }

    if (!thumbnail_drawn) {
        // Tool slot, operation slot, or empty: render a button
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
        } else if (slot.command != nullptr) {
            // Operation slot: a labeled button that invokes the stored operation
            // (with its frozen params) against the current selection on click.
            // ImGui::Button returns false when the click became a drag, so the
            // click-vs-drag distinction is handled for us.
            if (ImGui::Button(operation_short_label(slot.command), button_size) && (m_context.operations != nullptr)) {
                m_context.operations->run_operation(slot.command, slot.operation_params);
            }
        } else if (!slot.graph_node_type.empty()) {
            // Graph-node slot: a labeled button that spawns the node type in
            // its graph editor window (on the window's spawn grid) on click.
            if (ImGui::Button(slot.graph_node_label.c_str(), button_size)) {
                Graph_editor_window_base* window = Graph_editor_window_base::find_window_by_kind(m_context, slot.graph_node_kind.c_str());
                if (window != nullptr) {
                    window->spawn_node_from_slot(slot.graph_node_type);
                }
            }
        } else {
            ImGui::Button("##empty", button_size);
        }

        ImGui::PopStyleColor(3);

        // Tooltip for tools
        if ((slot.tool != nullptr) && !slot_brush && !slot_material && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", slot.tool->get_description());
        }
        // Tooltip for operation slots: the full (dotted) command name.
        if ((slot.tool == nullptr) && (slot.command != nullptr) && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", slot.command->get_name());
        }
        // Tooltip for graph-node slots: label plus the owning editor kind.
        if (!slot.graph_node_type.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s (%s node)", slot.graph_node_label.c_str(), slot.graph_node_kind.c_str());
        }
    }

    // Drag source
    if (is_source && has_item) {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            Slot_drag_payload drag{
                .tool     = slot.tool,
                .brush    = slot_brush.get(),
                .material = slot_material.get(),
                .command  = slot.command,
                .params   = slot.operation_params,
                .section  = static_cast<Slot_section>(section),
                .index    = slot_index
            };
            snprintf(drag.graph_node_kind,  sizeof(drag.graph_node_kind),  "%s", slot.graph_node_kind.c_str());
            snprintf(drag.graph_node_type,  sizeof(drag.graph_node_type),  "%s", slot.graph_node_type.c_str());
            snprintf(drag.graph_node_label, sizeof(drag.graph_node_label), "%s", slot.graph_node_label.c_str());
            ImGui::SetDragDropPayload(c_inventory_slot_payload_type, &drag, sizeof(Slot_drag_payload));
            if (slot_brush) {
                ImGui::Text("%s", slot_brush->get_name().c_str());
            } else if (slot_material) {
                ImGui::Text("%s", slot_material->get_name().c_str());
            } else if (slot.tool != nullptr) {
                ImGui::Text("%s", slot.tool->get_description());
            } else if (slot.command != nullptr) {
                ImGui::Text("%s", slot.command->get_name());
            } else if (!slot.graph_node_type.empty()) {
                ImGui::Text("%s", slot.graph_node_label.c_str());
            }
            ImGui::EndDragDropSource();
        }
    }

    // Right-click to clear
    if (is_target && has_item && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        slot.tool = nullptr;
        slot.brush   .set_key(Asset_key{}); // releases the usership
        slot.material.set_key(Asset_key{});
        slot.command = nullptr;
        slot.graph_node_kind.clear();
        slot.graph_node_type.clear();
        slot.graph_node_label.clear();
        changed = true;
    }

    // Drop target
    if (is_target) {
        if (ImGui::BeginDragDropTarget()) {
            // Accept inventory slot drops (swap)
            const ImGuiPayload* slot_payload = ImGui::AcceptDragDropPayload(c_inventory_slot_payload_type);
            if (slot_payload != nullptr) {
                const Slot_drag_payload& source = *static_cast<const Slot_drag_payload*>(slot_payload->Data);

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
                    // Swap the real entries (not a payload reconstruction):
                    // the Asset_references travel whole, so an unresolved key
                    // moves with its slot content and a resolved reference
                    // keeps its usership (re-registered by the copies).
                    Slot_entry old_dest = slot;
                    slot         = *source_slot;
                    *source_slot = old_dest;
                    set_slot_labels(*source_slot, source.section == Slot_section::hotbar, source.index);
                } else {
                    // Palette: copy only. Reconstruct from the payload;
                    // adopt() records exactly the dragged objects as this
                    // slot's asset userships.
                    Slot_entry source_entry;
                    source_entry.tool = source.tool;
                    if ((source.brush != nullptr) && (m_context.asset_manager != nullptr)) {
                        source_entry.brush.adopt(*m_context.asset_manager, std::dynamic_pointer_cast<erhe::Item_base>(source.brush->shared_from_this()));
                    }
                    if ((source.material != nullptr) && (m_context.asset_manager != nullptr)) {
                        source_entry.material.adopt(*m_context.asset_manager, std::dynamic_pointer_cast<erhe::Item_base>(source.material->shared_from_this()));
                    }
                    source_entry.command          = source.command;
                    source_entry.operation_params = source.params;
                    source_entry.graph_node_kind  = source.graph_node_kind;
                    source_entry.graph_node_type  = source.graph_node_type;
                    source_entry.graph_node_label = source.graph_node_label;
                    slot = source_entry;
                }
                changed = true;
            }

            // Accept an operation dragged out of the Operations window.
            const ImGuiPayload* op_payload = ImGui::AcceptDragDropPayload(c_operation_payload_type);
            if (op_payload != nullptr) {
                const Operation_drag_payload& op = *static_cast<const Operation_drag_payload*>(op_payload->Data);
                slot.tool = nullptr;
                slot.brush   .set_key(Asset_key{});
                slot.material.set_key(Asset_key{});
                slot.command          = op.command;
                slot.operation_params = op.params;
                slot.graph_node_kind.clear();
                slot.graph_node_type.clear();
                slot.graph_node_label.clear();
                changed = true;
            }

            // Accept a node type dragged out of a graph editor's node palette.
            const ImGuiPayload* graph_node_payload = ImGui::AcceptDragDropPayload(c_graph_node_payload_type);
            if (graph_node_payload != nullptr) {
                const Graph_node_drag_payload& node = *static_cast<const Graph_node_drag_payload*>(graph_node_payload->Data);
                slot.tool = nullptr;
                slot.brush   .set_key(Asset_key{});
                slot.material.set_key(Asset_key{});
                slot.command          = nullptr;
                slot.graph_node_kind  = node.kind;
                slot.graph_node_type  = node.type_name;
                slot.graph_node_label = node.label;
                changed = true;
            }

            // Accept content library node drops (brushes and materials)
            const ImGuiPayload* node_payload = ImGui::AcceptDragDropPayload("Content_library_node");
            if ((node_payload != nullptr) && (m_context.asset_manager != nullptr)) {
                erhe::Item_base* item_base = *static_cast<erhe::Item_base**>(node_payload->Data);
                Content_library_node* node = dynamic_cast<Content_library_node*>(item_base);
                if (node != nullptr) {
                    std::shared_ptr<Brush> dropped_brush = std::dynamic_pointer_cast<Brush>(node->item);
                    if (dropped_brush) {
                        slot.brush.adopt(*m_context.asset_manager, dropped_brush);
                        slot.material.set_key(Asset_key{});
                        slot.tool     = m_context.brush_tool;
                        slot.command  = nullptr;
                        slot.graph_node_kind.clear();
                        slot.graph_node_type.clear();
                        slot.graph_node_label.clear();
                        changed       = true;
                    }
                    std::shared_ptr<erhe::primitive::Material> dropped_material =
                        std::dynamic_pointer_cast<erhe::primitive::Material>(node->item);
                    if (dropped_material) {
                        const std::shared_ptr<Brush> current_brush = slot.get_brush();
                        if (current_brush) {
                            // Slot has a brush: find or create a forked brush with this material
                            slot.brush.adopt(*m_context.asset_manager, find_or_create_brush_with_material(current_brush, dropped_material));
                        } else {
                            // Empty or tool slot: make it a material slot
                            slot.material.adopt(*m_context.asset_manager, dropped_material);
                            slot.brush.set_key(Asset_key{});
                            slot.tool     = m_context.material_paint_tool;
                        }
                        slot.command = nullptr;
                        slot.graph_node_kind.clear();
                        slot.graph_node_type.clear();
                        slot.graph_node_label.clear();
                        changed = true;
                    }
                }
            }

            ImGui::EndDragDropTarget();
        }
    }

    ImGui::PopID();

    // Re-stamp the usership labels after any slot content change: swaps and
    // palette copies bring references carrying another slot's (or an empty)
    // label.
    if (changed && ((section == 1) || (section == 2))) {
        set_slot_labels(slot, section == 2, slot_index);
    }

    return changed;
}

auto Inventory_window::adopt_into_grid_slot(const int slot_index, const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    if ((slot_index < 0) || (slot_index >= static_cast<int>(m_grid_slots.size())) || (m_context.asset_manager == nullptr)) {
        return false;
    }
    Slot_entry& slot = m_grid_slots.at(static_cast<std::size_t>(slot_index));
    if (!item) {
        slot.brush   .set_key(Asset_key{});
        slot.material.set_key(Asset_key{});
        set_slot_labels(slot, false, slot_index);
        return true;
    }
    const std::shared_ptr<Brush> brush = std::dynamic_pointer_cast<Brush>(item);
    if (brush) {
        slot.brush.adopt(*m_context.asset_manager, brush);
        slot.material.set_key(Asset_key{});
        slot.tool = m_context.brush_tool;
        set_slot_labels(slot, false, slot_index);
        return true;
    }
    const std::shared_ptr<erhe::primitive::Material> material = std::dynamic_pointer_cast<erhe::primitive::Material>(item);
    if (material) {
        slot.material.adopt(*m_context.asset_manager, material);
        slot.brush.set_key(Asset_key{});
        slot.tool = m_context.material_paint_tool;
        set_slot_labels(slot, false, slot_index);
        return true;
    }
    return false;
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
    // Retry brush / material references whose content library was not loaded
    // yet at collect_tools() time (asynchronously loading scenes).
    if (resolve_slot_references()) {
        apply_hotbar();
    }

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
    // Pass all hotbar slots verbatim, including empty ones, so the hotbar renders a
    // fixed-width row (Minecraft-style) and the number-key -> slot mapping stays
    // positionally stable: key N always activates the N-th slot, whether or not the
    // slots before it are filled. Empty slots render as placeholder boxes in the
    // hotbar (see Hotbar::slot_button).
    m_context.hotbar->set_slots(m_hotbar_slots);
}

void Inventory_window::write_config(Inventory_config& config) const
{
    config.column_count      = m_column_count;
    config.row_count         = m_row_count;
    config.hotbar_slot_count = m_hotbar_slot_count;

    // The Asset_reference keys persist whether or not they ever resolved
    // this session (their content library may not have loaded) - saving must
    // not degrade a brush / material slot to its bare tool. Resolved
    // references write self-healed keys (uid learned on resolve). The legacy
    // brush_name / material_name fields are read-only since v4 and no longer
    // written.
    const auto write_slots = [](const std::vector<Slot_entry>& entries, std::vector<Inventory_slot>& out_slots) {
        out_slots.clear();
        for (const Slot_entry& entry : entries) {
            Inventory_slot slot;
            slot.tool_name        = (entry.tool != nullptr) ? entry.tool->get_description() : "";
            slot.brush_asset      = to_asset_reference_data(entry.brush.get_key());
            slot.material_asset   = to_asset_reference_data(entry.material.get_key());
            slot.command_name     = (entry.command != nullptr) ? entry.command->get_name() : "";
            slot.operation_params = entry.operation_params;
            slot.graph_node_kind  = entry.graph_node_kind;
            slot.graph_node_type  = entry.graph_node_type;
            slot.graph_node_label = entry.graph_node_label;
            out_slots.push_back(slot);
        }
    };
    write_slots(m_grid_slots,   config.grid_slots);
    write_slots(m_hotbar_slots, config.hotbar_slots);
}

}
