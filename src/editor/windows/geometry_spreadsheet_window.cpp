#include "windows/geometry_spreadsheet_window.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "tools/mesh_component_selection.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_imgui/imgui_item_recorder.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h> // IMGUI_TABLE_MAX_COLUMNS

#include <algorithm>

namespace editor {

namespace {

constexpr std::array<const char*, c_spreadsheet_domain_count> c_table_ids{
    "##spreadsheet_vertex",
    "##spreadsheet_corner",
    "##spreadsheet_facet",
    "##spreadsheet_edge"
};

constexpr std::array<const char*, c_spreadsheet_domain_count> c_tab_ids{
    "vertex",
    "corner",
    "facet",
    "edge"
};

} // anonymous namespace

Geometry_spreadsheet_window::Geometry_spreadsheet_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context,
    App_message_bus&             app_message_bus
)
    : Imgui_window{imgui_renderer, imgui_windows, "Geometry Spreadsheet", "geometry_spreadsheet"}
    , m_context   {app_context}
{
    // Target resolution runs on change only: these handlers set a flag that
    // the next drawn frame acts on (a hidden window does nothing).
    m_selection_subscription = app_message_bus.selection.subscribe(
        [this](Selection_message&) {
            on_selection_changed();
        }
    );
    m_active_item_subscription = app_message_bus.active_item.subscribe(
        [this](Active_item_changed_message&) {
            on_selection_changed();
        }
    );
    m_mode_subscription = app_message_bus.mesh_component_mode_changed.subscribe(
        [this](Mesh_component_mode_changed_message&) {
            on_selection_changed();
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            on_items_removed(*message.removed.get());
        }
    );
    m_geometry_changed_subscription = app_message_bus.mesh_geometry_changed.subscribe(
        [this](Mesh_geometry_changed_message& message) {
            on_geometry_changed(message);
        }
    );
}

void Geometry_spreadsheet_window::on_selection_changed()
{
    if (m_target_mode == Spreadsheet_target_mode::follow_selection) {
        m_follow_dirty = true;
    }
}

void Geometry_spreadsheet_window::on_items_removed(const Removed_items& removed)
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = m_target_mesh.lock();
    if (mesh && removed.lookup.contains(static_cast<const erhe::Item_base*>(mesh.get()))) {
        drop_target();
    }
}

void Geometry_spreadsheet_window::on_geometry_changed(const Mesh_geometry_changed_message& message)
{
    // A swapped Geometry is caught by the model's identity key; this covers
    // edits made in place on the same Geometry object (new attributes).
    if (message.mesh && (message.mesh == m_target_mesh.lock())) {
        m_model.invalidate();
    }
}

void Geometry_spreadsheet_window::drop_target()
{
    m_target_mesh.reset();
    m_primitive_index = 0;
    m_model.release();
    m_model_released = true;
    m_drawn_rows.range_count = 0;
}

void Geometry_spreadsheet_window::set_target(const std::shared_ptr<erhe::scene::Mesh>& mesh, const std::size_t primitive_index)
{
    m_target_mode     = Spreadsheet_target_mode::pinned;
    m_target_mesh     = mesh;
    m_primitive_index = primitive_index;
    m_follow_dirty    = false;
}

void Geometry_spreadsheet_window::set_target_mode(const Spreadsheet_target_mode mode)
{
    m_target_mode = mode;
    if (mode == Spreadsheet_target_mode::follow_selection) {
        m_follow_dirty = true;
    }
}

void Geometry_spreadsheet_window::set_domain(const Spreadsheet_domain domain)
{
    m_domain           = domain;
    m_requested_domain = domain;
    m_domain_requested = true;
}

auto Geometry_spreadsheet_window::get_target_mesh() const -> std::shared_ptr<erhe::scene::Mesh>
{
    return m_target_mesh.lock();
}

auto Geometry_spreadsheet_window::get_primitive_index() const -> std::size_t
{
    return m_primitive_index;
}

auto Geometry_spreadsheet_window::get_target_mode() const -> Spreadsheet_target_mode
{
    return m_target_mode;
}

auto Geometry_spreadsheet_window::get_domain() const -> Spreadsheet_domain
{
    return m_domain;
}

auto Geometry_spreadsheet_window::get_drawn_rows() const -> const Spreadsheet_drawn_rows&
{
    return m_drawn_rows;
}

auto Geometry_spreadsheet_window::get_model() const -> const Geometry_spreadsheet_model&
{
    return m_model;
}

void Geometry_spreadsheet_window::update_model()
{
    static_cast<void>(refresh_target());
}

void Geometry_spreadsheet_window::resolve_follow_target()
{
    // 1. A component selection on exactly one live mesh primitive.
    const Mesh_component_selection& component_selection = *m_context.mesh_component_selection;
    if (is_mesh_component_mode(component_selection.get_mode())) {
        const Mesh_component_entry* found = nullptr;
        std::size_t                 live_count = 0;
        for (const Mesh_component_entry& entry : component_selection.get_entries()) {
            if (!entry.is_empty() && component_selection.is_live(entry)) {
                found = &entry;
                ++live_count;
            }
        }
        if (live_count == 1) {
            m_target_mesh     = found->mesh;
            m_primitive_index = found->primitive_index;
            return;
        }
    }

    // 2. The active item's mesh, 3. the last selected mesh.
    Selection& selection = *m_context.selection;
    std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(selection.get_active_item());
    if (!mesh) {
        mesh = selection.get_last_selected<erhe::scene::Mesh>();
    }
    if (mesh != m_target_mesh.lock()) {
        m_primitive_index = 0;
    }
    m_target_mesh = mesh;
}

auto Geometry_spreadsheet_window::refresh_target() -> std::shared_ptr<erhe::scene::Mesh>
{
    if ((m_target_mode == Spreadsheet_target_mode::follow_selection) && m_follow_dirty) {
        m_follow_dirty = false;
        resolve_follow_target();
    }

    std::shared_ptr<erhe::scene::Mesh> mesh = m_target_mesh.lock();
    if (!mesh) {
        if (!m_model_released) {
            drop_target();
        }
        return {};
    }
    // Validate on access (doc/editor/coding_rules.md "Scene-hosted references
    // in editor parts"): a mesh taken out of its scene, or of a closed scene,
    // is no longer a target.
    const erhe::Item_host* item_host = mesh->get_item_host();
    if ((item_host == nullptr) || !m_context.app_scenes->is_host_registered(item_host)) {
        drop_target();
        return {};
    }

    std::shared_ptr<erhe::geometry::Geometry> geometry{};
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if (m_primitive_index < primitives.size()) {
        const std::shared_ptr<erhe::primitive::Primitive>& primitive = primitives[m_primitive_index].primitive;
        if (primitive && primitive->render_shape) {
            // Never blocks; null until the shape's geometry is published.
            geometry = primitive->render_shape->get_geometry_const();
        }
    }
    m_model.set_geometry(geometry);
    m_model_released = false;
    m_model.update(m_domain);
    return mesh;
}

void Geometry_spreadsheet_window::hidden()
{
    if (!m_model_released) {
        m_model.release();
        m_model_released = true;
        m_drawn_rows.range_count = 0;
        std::vector<float>{}.swap(m_column_widths);
        m_widths_serial = 0;
    }
}

void Geometry_spreadsheet_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<erhe::scene::Mesh> mesh = refresh_target();
    imgui_target_row(mesh);
    if (!mesh) {
        m_drawn_rows.range_count = 0;
        ImGui::TextDisabled("No mesh selected");
        return;
    }
    if (!m_model.get_geometry()) {
        m_drawn_rows.range_count = 0;
        ImGui::TextDisabled("Geometry not available");
        return;
    }
    imgui_domain_tabs();
    imgui_table();
}

void Geometry_spreadsheet_window::imgui_target_row(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    if (mesh) {
        const std::string& name = mesh->get_name();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name.data(), name.data() + name.size());
        ImGui::SameLine();
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
        if (primitives.size() > 1) {
            int primitive_index = static_cast<int>(m_primitive_index);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
            if (ImGui::InputInt("Primitive", &primitive_index)) {
                m_primitive_index = static_cast<std::size_t>(std::clamp(primitive_index, 0, static_cast<int>(primitives.size()) - 1));
            }
            ImGui::SameLine();
        }
    }

    bool pinned = (m_target_mode == Spreadsheet_target_mode::pinned);
    if (ImGui::Checkbox("Pin", &pinned)) {
        set_target_mode(pinned ? Spreadsheet_target_mode::pinned : Spreadsheet_target_mode::follow_selection);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Keep showing this mesh when the selection changes");
    }

    ImGui::SameLine();
    int precision = m_model.get_precision();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    if (ImGui::SliderInt("Decimals", &precision, 0, 9, (precision == 0) ? "shortest" : "%d")) {
        m_model.set_precision(precision);
    }
}

void Geometry_spreadsheet_window::imgui_domain_tabs()
{
    const erhe::geometry::Geometry& geometry = *m_model.get_geometry().get();
    if (!ImGui::BeginTabBar("##domains")) {
        return;
    }
    Spreadsheet_domain selected = m_domain;
    std::array<char, 64> label{};
    for (std::size_t i = 0; i < c_spreadsheet_domain_count; ++i) {
        const Spreadsheet_domain domain = static_cast<Spreadsheet_domain>(i);
        const fmt::format_to_n_result<char*> result = fmt::format_to_n(
            label.data(), label.size() - 1, "{} ({})###{}", c_str(domain), get_domain_element_count(geometry, domain), c_tab_ids[i]
        );
        *result.out = '\0';
        const ImGuiTabItemFlags flags = (m_domain_requested && (m_requested_domain == domain)) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        const bool open = ImGui::BeginTabItem(label.data(), nullptr, flags);
        // The tab label carries a count; the recorded label is the plain
        // domain name, so MCP UI driving addresses the tab as "Vertex" etc.
        erhe::imgui::set_item_debug_label(c_str(domain));
        if (open) {
            selected = domain;
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
    m_domain_requested = false;
    if (selected != m_domain) {
        m_domain = selected;
        m_model.update(m_domain);
    }
}

void Geometry_spreadsheet_window::imgui_table()
{
    const std::span<const Spreadsheet_column> columns = m_model.get_columns(m_domain);
    const std::size_t                         row_count = m_model.get_row_count(m_domain);
    if ((m_domain == Spreadsheet_domain::edge) && (m_model.get_element_count(m_domain) == 0)) {
        m_drawn_rows.range_count = 0;
        ImGui::TextDisabled("Edges are not built for this geometry");
        return;
    }
    if (columns.empty()) {
        m_drawn_rows.range_count = 0;
        return;
    }
    const int column_count = static_cast<int>(std::min(columns.size(), static_cast<std::size_t>(IMGUI_TABLE_MAX_COLUMNS)));

    // Initial column widths: measured once per layout (and font size), used by
    // ImGui only when it first creates the table's columns.
    const uint64_t layout_serial = m_model.get_layout_serial(m_domain);
    const float    font_size     = ImGui::GetFontSize();
    if ((m_widths_serial != layout_serial) || (m_widths_domain != m_domain) || (m_widths_font_size != font_size)) {
        m_widths_serial    = layout_serial;
        m_widths_domain    = m_domain;
        m_widths_font_size = font_size;
        const float float_width = ImGui::CalcTextSize("-0000.0000").x;
        const float int_width   = ImGui::CalcTextSize("0000000").x;
        m_column_widths.clear();
        for (int c = 0; c < column_count; ++c) {
            const Spreadsheet_column& column = columns[static_cast<std::size_t>(c)];
            const float value_width = (column.value_type == Spreadsheet_value_type::f32) ? float_width : int_width;
            m_column_widths.push_back(std::max(value_width, ImGui::CalcTextSize(column.get_label()).x));
        }
    }

    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_ScrollX       |
        ImGuiTableFlags_ScrollY       |
        ImGuiTableFlags_RowBg         |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_BordersOuter  |
        ImGuiTableFlags_Resizable     |
        ImGuiTableFlags_Hideable      |
        ImGuiTableFlags_Sortable      |
        ImGuiTableFlags_SortTristate  |
        ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable(c_table_ids[static_cast<std::size_t>(m_domain)], column_count, table_flags, ImVec2{0.0f, 0.0f})) {
        m_drawn_rows.range_count = 0;
        return;
    }
    ImGui::TableSetupScrollFreeze(1, 1);
    for (int c = 0; c < column_count; ++c) {
        const ImGuiTableColumnFlags column_flags = (c == 0) ? ImGuiTableColumnFlags_NoHide : ImGuiTableColumnFlags_None;
        ImGui::TableSetupColumn(columns[static_cast<std::size_t>(c)].get_label(), column_flags | ImGuiTableColumnFlags_WidthFixed, m_column_widths[static_cast<std::size_t>(c)]);
    }
    ImGui::TableHeadersRow();

    // Sort specs are dirty only on the frame the user changes them.
    ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs();
    if ((sort_specs != nullptr) && sort_specs->SpecsDirty) {
        if (sort_specs->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs& spec = sort_specs->Specs[0];
            m_model.set_sort(
                m_domain,
                spec.ColumnIndex,
                (spec.SortDirection == ImGuiSortDirection_Descending) ? Sort_direction::descending : Sort_direction::ascending
            );
        } else {
            m_model.set_sort(m_domain, -1, Sort_direction::ascending);
        }
        sort_specs->SpecsDirty = false;
        m_model.update(m_domain);
    }

    const ImVec4 absent_color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    std::array<char, 64> buffer{};
    m_drawn_rows.range_count = 0;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(row_count));
    while (clipper.Step()) {
        if (clipper.DisplayStart >= clipper.DisplayEnd) {
            continue;
        }
        Spreadsheet_drawn_rows::Range range{
            .first = static_cast<std::size_t>(clipper.DisplayStart),
            .last  = static_cast<std::size_t>(clipper.DisplayEnd)
        };
        if ((m_drawn_rows.range_count > 0) && (m_drawn_rows.ranges[m_drawn_rows.range_count - 1].last == range.first)) {
            m_drawn_rows.ranges[m_drawn_rows.range_count - 1].last = range.last;
        } else if (m_drawn_rows.range_count < Spreadsheet_drawn_rows::c_max_ranges) {
            m_drawn_rows.ranges[m_drawn_rows.range_count++] = range;
        }
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const GEO::index_t element = m_model.get_row_element(m_domain, static_cast<std::size_t>(row));
            ImGui::TableNextRow();
            for (int c = 0; c < column_count; ++c) {
                // False for a column scrolled out horizontally or hidden: skip
                // it before reading or formatting anything.
                if (!ImGui::TableSetColumnIndex(c)) {
                    continue;
                }
                const Formatted_cell cell  = m_model.format_cell(columns[static_cast<std::size_t>(c)], element, buffer);
                const char*          begin = cell.text.data();
                const char*          end   = cell.text.data() + cell.text.size();
                const float          width = ImGui::CalcTextSize(begin, end).x;
                const float          avail = ImGui::GetContentRegionAvail().x;
                if (avail > width) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width);
                }
                if (cell.present) {
                    ImGui::TextUnformatted(begin, end);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, absent_color);
                    ImGui::TextUnformatted(begin, end);
                    ImGui::PopStyleColor();
                }
            }
        }
    }
    clipper.End();
    ImGui::EndTable();
}

} // namespace editor
