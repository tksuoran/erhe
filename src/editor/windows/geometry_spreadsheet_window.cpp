#include "windows/geometry_spreadsheet_window.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "editor_log.hpp"
#include "operations/operation_stack.hpp"
#include "operations/set_geometry_attribute_operation.hpp"
#include "tools/mesh_component_selection.hpp"
#include "tools/mesh_component_selection_tool.hpp"
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
#include <charconv>

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
    m_components_changed_subscription = app_message_bus.mesh_component_selection_changed.subscribe(
        [this](Mesh_component_selection_changed_message&) {
            m_row_filter_dirty = true;
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
        m_row_filter_dirty = true;
    }
}

void Geometry_spreadsheet_window::drop_target()
{
    set_hovered_element({}, GEO::NO_INDEX);
    m_row_filter_dirty = true;
    m_target_mesh.reset();
    m_primitive_index = 0;
    m_model.release();
    m_model_released = true;
    m_drawn_rows.range_count = 0;
}

void Geometry_spreadsheet_window::set_target(const std::shared_ptr<erhe::scene::Mesh>& mesh, const std::size_t primitive_index)
{
    m_target_mode      = Spreadsheet_target_mode::pinned;
    m_target_mesh      = mesh;
    m_primitive_index  = primitive_index;
    m_follow_dirty     = false;
    m_row_filter_dirty = true;
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
    m_row_filter_dirty = true;
}

auto Geometry_spreadsheet_window::get_row_filter() const -> Spreadsheet_row_filter
{
    return m_row_filter;
}

void Geometry_spreadsheet_window::set_row_filter(const Spreadsheet_row_filter row_filter)
{
    if (m_row_filter == row_filter) {
        return;
    }
    m_row_filter       = row_filter;
    m_row_filter_dirty = true;
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
            m_target_mesh      = found->mesh;
            m_primitive_index  = found->primitive_index;
            m_row_filter_dirty = true;
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
        m_primitive_index  = 0;
        m_row_filter_dirty = true;
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
    if (m_model.set_geometry(geometry)) {
        m_row_filter_dirty = true;
    }
    m_model_released = false;
    if (m_row_filter_dirty) {
        update_row_filter(mesh);
    }
    m_model.update(m_domain);
    return mesh;
}

void Geometry_spreadsheet_window::hidden()
{
    if (m_hovered_element != GEO::NO_INDEX) {
        set_hovered_element({}, GEO::NO_INDEX);
    }
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
    imgui_table(mesh);
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
    bool selected_only = (m_row_filter == Spreadsheet_row_filter::selected);
    if (ImGui::Checkbox("Selected Only", &selected_only)) {
        set_row_filter(selected_only ? Spreadsheet_row_filter::selected : Spreadsheet_row_filter::all);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show only the rows of the selected components");
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
        set_hovered_element({}, GEO::NO_INDEX);
        if (m_row_filter == Spreadsheet_row_filter::selected) {
            update_row_filter(m_target_mesh.lock());
        }
        m_model.update(m_domain);
    }
}

void Geometry_spreadsheet_window::imgui_table(const std::shared_ptr<erhe::scene::Mesh>& mesh)
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
    // The selection entry is looked up once per frame; rows test membership.
    const Mesh_component_entry* const entry           = (m_domain == Spreadsheet_domain::corner) ? nullptr : find_selection_entry(mesh);
    const GEO::Mesh&                  geo_mesh        = m_model.get_geometry()->get_mesh();
    GEO::index_t                      hovered_element = GEO::NO_INDEX;
    std::size_t                       clicked_row     = 0;
    GEO::index_t                      clicked_element = GEO::NO_INDEX;
    // An edit ends when its row no longer shows the edited cell (a layout
    // rebuild, a sort or filter change, a tab switch).
    if (m_edit.active && (
        (m_edit.domain != m_domain) ||
        (m_edit.layout_serial != layout_serial) ||
        (m_edit.row >= row_count) ||
        (m_model.get_row_element(m_domain, m_edit.row) != m_edit.element)
    )) {
        m_edit.active = false;
    }

    std::size_t  double_clicked_row     = 0;
    GEO::index_t double_clicked_element = GEO::NO_INDEX;
    GEO::index_t right_clicked_element  = GEO::NO_INDEX;
    m_edit_commit = false;

    m_drawn_rows.range_count = 0;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(row_count));
    // The edited row stays submitted while scrolled out of view, so its input
    // field keeps keyboard focus.
    if (m_edit.active) {
        clipper.IncludeItemByIndex(static_cast<int>(m_edit.row));
    }
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

            // Index cell: a row-spanning selectable (frozen column, always visible).
            bool selected = false;
            switch (m_domain) {
                case Spreadsheet_domain::vertex: selected = (entry != nullptr) && entry->vertices.contains(element); break;
                case Spreadsheet_domain::facet:  selected = (entry != nullptr) && entry->facets.contains(element); break;
                case Spreadsheet_domain::edge:   selected = (entry != nullptr) && entry->edges.contains(make_edge_key(geo_mesh.edges.vertex(element, 0), geo_mesh.edges.vertex(element, 1))); break;
                case Spreadsheet_domain::corner: selected = m_corner_selection.contains(element); break;
                default: break;
            }
            ImGui::TableSetColumnIndex(0);
            const std::to_chars_result index_text = std::to_chars(buffer.data(), buffer.data() + buffer.size() - 1, element);
            *index_text.ptr = '\0';
            ImGui::PushID(static_cast<int>(element));
            if (ImGui::Selectable(buffer.data(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                clicked_row     = static_cast<std::size_t>(row);
                clicked_element = element;
            }
            if (erhe::imgui::is_item_recording()) {
                const fmt::format_to_n_result<char*> result = fmt::format_to_n(buffer.data(), buffer.size() - 1, "row {}", element);
                *result.out = '\0';
                erhe::imgui::set_item_debug_label(buffer.data());
            }
            if (ImGui::IsItemHovered()) {
                hovered_element = element;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    double_clicked_row     = static_cast<std::size_t>(row);
                    double_clicked_element = element;
                }
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                right_clicked_element = element;
            }
            ImGui::PopID();

            for (int c = 1; c < column_count; ++c) {
                // False for a column scrolled out horizontally or hidden: skip
                // it before reading or formatting anything.
                if (!ImGui::TableSetColumnIndex(c)) {
                    continue;
                }
                if (m_edit.active && (m_edit.element == element) && (m_edit.column == c)) {
                    imgui_cell_editor(columns[static_cast<std::size_t>(c)]);
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
    const bool table_hovered  = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const int  hovered_column = ImGui::TableGetHoveredColumn();
    ImGui::EndTable();

    if (m_edit_commit) {
        m_edit_commit = false;
        m_edit.active = false;
        if (static_cast<std::size_t>(m_edit.column) < columns.size()) {
            m_selected_scratch.clear();
            m_selected_scratch.push_back(m_edit.element);
            apply_cell_value(mesh, columns[static_cast<std::size_t>(m_edit.column)], m_selected_scratch, m_edit.value, Cell_value_op::set);
        }
    }
    if ((double_clicked_element != GEO::NO_INDEX) && (hovered_column > 0) && (hovered_column < column_count)) {
        begin_cell_edit(double_clicked_row, double_clicked_element, hovered_column);
    }
    if ((right_clicked_element != GEO::NO_INDEX) && (hovered_column > 0) && (hovered_column < column_count)) {
        m_menu_element = right_clicked_element;
        m_menu_column  = hovered_column;
        ImGui::OpenPopup("##cell_menu");
    }
    imgui_cell_menu(mesh);

    // The viewport hover highlight follows the hovered row; the tool is told
    // only when the hovered row changes.
    set_hovered_element(mesh, table_hovered ? hovered_element : GEO::NO_INDEX);
    if (clicked_element != GEO::NO_INDEX) {
        on_row_clicked(mesh, clicked_row, clicked_element);
    }
}

auto Geometry_spreadsheet_window::find_selection_entry(const std::shared_ptr<erhe::scene::Mesh>& mesh) -> Mesh_component_entry*
{
    if (!mesh) {
        return nullptr;
    }
    return m_context.mesh_component_selection->find_entry(mesh, m_primitive_index, m_model.get_geometry());
}

auto Geometry_spreadsheet_window::is_element_selected(const GEO::index_t element) -> bool
{
    const std::shared_ptr<erhe::geometry::Geometry>& geometry = m_model.get_geometry();
    if (!geometry) {
        return false;
    }
    if (m_domain == Spreadsheet_domain::corner) {
        return m_corner_selection.contains(element);
    }
    const Mesh_component_entry* entry = find_selection_entry(m_target_mesh.lock());
    if (entry == nullptr) {
        return false;
    }
    switch (m_domain) {
        case Spreadsheet_domain::vertex: return entry->vertices.contains(element);
        case Spreadsheet_domain::facet:  return entry->facets.contains(element);
        case Spreadsheet_domain::edge: {
            const GEO::Mesh& geo_mesh = geometry->get_mesh();
            return (element < geo_mesh.edges.nb()) && entry->edges.contains(make_edge_key(geo_mesh.edges.vertex(element, 0), geo_mesh.edges.vertex(element, 1)));
        }
        default: return false;
    }
}

void Geometry_spreadsheet_window::update_row_filter(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    m_row_filter_dirty = false;
    if (m_row_filter == Spreadsheet_row_filter::all) {
        for (std::size_t i = 0; i < c_spreadsheet_domain_count; ++i) {
            m_model.clear_row_filter(static_cast<Spreadsheet_domain>(i));
        }
        return;
    }
    const std::shared_ptr<erhe::geometry::Geometry>& geometry = m_model.get_geometry();
    m_row_filter_scratch.clear();
    if (m_domain == Spreadsheet_domain::corner) {
        m_row_filter_scratch.assign(m_corner_selection.begin(), m_corner_selection.end());
    } else if (geometry) {
        const Mesh_component_entry* entry = find_selection_entry(mesh);
        if (entry != nullptr) {
            switch (m_domain) {
                case Spreadsheet_domain::vertex: m_row_filter_scratch.assign(entry->vertices.begin(), entry->vertices.end()); break;
                case Spreadsheet_domain::facet:  m_row_filter_scratch.assign(entry->facets.begin(), entry->facets.end()); break;
                case Spreadsheet_domain::edge: {
                    if (geometry->has_edge_connectivity()) {
                        for (const Mesh_edge_key& key : entry->edges) {
                            const GEO::index_t edge = geometry->get_edge(key.first, key.second);
                            if (edge != GEO::NO_INDEX) {
                                m_row_filter_scratch.push_back(edge);
                            }
                        }
                        std::sort(m_row_filter_scratch.begin(), m_row_filter_scratch.end());
                    }
                    break;
                }
                default: break;
            }
        }
    }
    m_model.set_row_filter(m_domain, m_row_filter_scratch);
}

void Geometry_spreadsheet_window::on_row_clicked(const std::shared_ptr<erhe::scene::Mesh>& mesh, const std::size_t row, const GEO::index_t element)
{
    const ImGuiIO&    io        = ImGui::GetIO();
    const bool        ctrl      = io.KeyCtrl;
    const bool        shift     = io.KeyShift;
    const std::size_t row_count = m_model.get_row_count(m_domain);
    const std::size_t last_row  = (row_count > 0) ? (row_count - 1) : 0;
    const std::size_t first     = std::min(std::min(m_anchor_row, row), last_row);
    const std::size_t last      = std::min(std::max(m_anchor_row, row), last_row);

    if (m_domain == Spreadsheet_domain::corner) {
        // Corners are no component kind of Mesh_component_selection; the Corner
        // tab keeps its own row selection.
        if (shift) {
            for (std::size_t r = first; r <= last; ++r) {
                m_corner_selection.insert(m_model.get_row_element(m_domain, r));
            }
        } else if (ctrl) {
            if (m_corner_selection.erase(element) == 0) {
                m_corner_selection.insert(element);
            }
        } else {
            m_corner_selection.clear();
            m_corner_selection.insert(element);
        }
        if (!shift) {
            m_anchor_row = row;
        }
        m_row_filter_dirty = true;
        return;
    }

    // Vertex / Facet / Edge rows are the mesh component selection: the click
    // switches to the matching component mode so the viewport shows it, and
    // edits the target's entry with the viewport click rules (plain click
    // replaces, Ctrl toggles; Shift adds the display-order range).
    Mesh_component_selection& selection = *m_context.mesh_component_selection;
    const Mesh_component_mode mode =
        (m_domain == Spreadsheet_domain::vertex) ? Mesh_component_mode::vertex :
        (m_domain == Spreadsheet_domain::facet)  ? Mesh_component_mode::face   :
                                                   Mesh_component_mode::edge;
    selection.set_mode(mode);
    if (!ctrl && !shift) {
        selection.clear_all();
    }
    Mesh_component_entry& entry = selection.find_or_create_entry(mesh, m_primitive_index, m_model.get_geometry());
    if (shift) {
        for (std::size_t r = first; r <= last; ++r) {
            select_element(entry, m_model.get_row_element(m_domain, r), Row_select_op::add);
        }
    } else {
        select_element(entry, element, ctrl ? Row_select_op::toggle : Row_select_op::add);
        m_anchor_row = row;
    }
}

void Geometry_spreadsheet_window::select_element(Mesh_component_entry& entry, const GEO::index_t element, const Row_select_op op)
{
    switch (m_domain) {
        case Spreadsheet_domain::vertex: {
            if (op == Row_select_op::toggle) { entry.toggle_vertex(element); } else { entry.add_vertex(element); }
            break;
        }
        case Spreadsheet_domain::facet: {
            if (op == Row_select_op::toggle) { entry.toggle_facet(element); } else { entry.add_facet(element); }
            break;
        }
        case Spreadsheet_domain::edge: {
            const GEO::Mesh&   geo_mesh = m_model.get_geometry()->get_mesh();
            const GEO::index_t v0       = geo_mesh.edges.vertex(element, 0);
            const GEO::index_t v1       = geo_mesh.edges.vertex(element, 1);
            if (op == Row_select_op::toggle) { entry.toggle_edge(v0, v1); } else { entry.add_edge(v0, v1); }
            break;
        }
        default: {
            break;
        }
    }
}

void Geometry_spreadsheet_window::set_hovered_element(const std::shared_ptr<erhe::scene::Mesh>& mesh, const GEO::index_t element)
{
    if ((element == m_hovered_element) && (m_domain == m_hovered_domain)) {
        return;
    }
    m_hovered_element = element;
    m_hovered_domain  = m_domain;
    Mesh_component_selection_tool* tool = m_context.mesh_component_selection_tool;
    if (tool == nullptr) {
        return;
    }
    const std::shared_ptr<erhe::geometry::Geometry>& geometry = m_model.get_geometry();
    if ((element == GEO::NO_INDEX) || !mesh || !geometry) {
        tool->clear_external_hover();
        return;
    }
    switch (m_domain) {
        case Spreadsheet_domain::vertex: {
            tool->set_external_hover(mesh, geometry, Mesh_component_mode::vertex, element, 0, 0);
            break;
        }
        case Spreadsheet_domain::facet: {
            tool->set_external_hover(mesh, geometry, Mesh_component_mode::face, element, 0, 0);
            break;
        }
        case Spreadsheet_domain::edge: {
            const GEO::Mesh& geo_mesh = geometry->get_mesh();
            tool->set_external_hover(mesh, geometry, Mesh_component_mode::edge, element, geo_mesh.edges.vertex(element, 0), geo_mesh.edges.vertex(element, 1));
            break;
        }
        default: {
            // A corner is no component kind the viewport highlights.
            tool->clear_external_hover();
            break;
        }
    }
}


void Geometry_spreadsheet_window::gather_selected_elements(const std::shared_ptr<erhe::scene::Mesh>& mesh, std::vector<GEO::index_t>& out)
{
    out.clear();
    const std::shared_ptr<erhe::geometry::Geometry>& geometry = m_model.get_geometry();
    if (m_domain == Spreadsheet_domain::corner) {
        out.assign(m_corner_selection.begin(), m_corner_selection.end());
        return;
    }
    const Mesh_component_entry* entry = find_selection_entry(mesh);
    if (!geometry || (entry == nullptr)) {
        return;
    }
    switch (m_domain) {
        case Spreadsheet_domain::vertex: out.assign(entry->vertices.begin(), entry->vertices.end()); break;
        case Spreadsheet_domain::facet:  out.assign(entry->facets.begin(), entry->facets.end()); break;
        case Spreadsheet_domain::edge: {
            if (geometry->has_edge_connectivity()) {
                for (const Mesh_edge_key& key : entry->edges) {
                    const GEO::index_t edge = geometry->get_edge(key.first, key.second);
                    if (edge != GEO::NO_INDEX) {
                        out.push_back(edge);
                    }
                }
                std::sort(out.begin(), out.end());
            }
            break;
        }
        default: break;
    }
}

void Geometry_spreadsheet_window::begin_cell_edit(const std::size_t row, const GEO::index_t element, const int column)
{
    const std::span<const Spreadsheet_column> columns = m_model.get_columns(m_domain);
    if (static_cast<std::size_t>(column) >= columns.size()) {
        return;
    }
    const Spreadsheet_column& spreadsheet_column = columns[static_cast<std::size_t>(column)];
    if (spreadsheet_column.edit != Spreadsheet_edit::editable) {
        return;
    }
    double value{0.0};
    static_cast<void>(m_model.read_cell(spreadsheet_column, element, value)); // an absent value starts at 0
    m_edit = Spreadsheet_cell_edit{
        .active        = true,
        .request_focus = true,
        .was_active    = false,
        .frames        = 0,
        .domain        = m_domain,
        .row           = row,
        .element       = element,
        .column        = column,
        .layout_serial = m_model.get_layout_serial(m_domain),
        .value         = value,
        .original      = value
    };
}

void Geometry_spreadsheet_window::imgui_cell_editor(const Spreadsheet_column& column)
{
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (m_edit.request_focus) {
        ImGui::SetKeyboardFocusHere();
        m_edit.request_focus = false;
    }
    const char* format = (column.value_type == Spreadsheet_value_type::f32) ? "%.9g" : "%.0f";
    ImGui::InputScalar("##cell_edit", ImGuiDataType_Double, &m_edit.value, nullptr, nullptr, format);
    ++m_edit.frames;
    if (ImGui::IsItemActive()) {
        m_edit.was_active = true;
    }
    // The commit is the edit's change site: Enter or focus loss with a new
    // value. Escape restores the original value, so it commits nothing.
    if (ImGui::IsItemDeactivated()) {
        if (m_edit.value != m_edit.original) {
            m_edit_commit = true;
        } else {
            m_edit.active = false;
        }
    } else if (!m_edit.was_active && (m_edit.frames > 2)) {
        m_edit.active = false; // focus never arrived (e.g. another item took it)
    }
}

void Geometry_spreadsheet_window::imgui_cell_menu(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    if (!ImGui::BeginPopup("##cell_menu")) {
        return;
    }
    const std::span<const Spreadsheet_column> columns = m_model.get_columns(m_domain);
    if ((m_menu_column <= 0) || (static_cast<std::size_t>(m_menu_column) >= columns.size()) || (m_menu_element == GEO::NO_INDEX)) {
        ImGui::EndPopup();
        return;
    }
    const Spreadsheet_column& column = columns[static_cast<std::size_t>(m_menu_column)];
    std::array<char, 64> buffer{};
    const Formatted_cell cell = m_model.format_cell(column, m_menu_element, buffer);
    ImGui::TextDisabled("%s of %s %u: %.*s", column.get_label(), c_str(m_domain), m_menu_element, static_cast<int>(cell.text.size()), cell.text.data());
    ImGui::Separator();
    if (column.edit != Spreadsheet_edit::editable) {
        ImGui::TextDisabled("Read-only column");
        ImGui::EndPopup();
        return;
    }
    gather_selected_elements(mesh, m_selected_scratch);
    double value{0.0};
    const bool present = m_model.read_cell(column, m_menu_element, value);
    if (ImGui::MenuItem("Set Selected Rows To This Value", nullptr, false, present && !m_selected_scratch.empty())) {
        apply_cell_value(mesh, column, m_selected_scratch, value, Cell_value_op::set);
    }
    if (column.kind == Spreadsheet_column_kind::attribute) {
        if (ImGui::MenuItem("Remove Value", nullptr, false, present)) {
            m_selected_scratch.clear();
            m_selected_scratch.push_back(m_menu_element);
            apply_cell_value(mesh, column, m_selected_scratch, 0.0, Cell_value_op::remove);
        }
    }
    ImGui::EndPopup();
}

void Geometry_spreadsheet_window::apply_cell_value(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    const Spreadsheet_column&                 column,
    const std::vector<GEO::index_t>&          elements,
    const double                              value,
    const Cell_value_op                       op
)
{
    const std::shared_ptr<erhe::geometry::Geometry>& geometry = m_model.get_geometry();
    if (!mesh || !geometry || elements.empty()) {
        return;
    }
    const std::string_view attribute =
        (column.kind == Spreadsheet_column_kind::vertex_position) ? c_position_attribute :
        (column.kind == Spreadsheet_column_kind::attribute)       ? std::string_view{column.attribute_name} :
                                                                    std::string_view{};
    if (attribute.empty()) {
        return;
    }
    // Each element keeps its other components; an element without a value
    // gets zeros, with w = 1 for a four-component attribute (opaque color,
    // right-handed tangent).
    std::vector<Geometry_attribute_value> values;
    values.reserve(elements.size());
    for (const GEO::index_t element : elements) {
        const std::optional<Geometry_attribute_value> before = read_geometry_attribute(*geometry.get(), attribute, element);
        if (!before.has_value()) {
            return;
        }
        Geometry_attribute_value after = before.value();
        if (op == Cell_value_op::remove) {
            after.present = false;
        } else {
            if (!after.present) {
                after.components = {0.0, 0.0, 0.0, (column.component_count == 4) ? 1.0 : 0.0};
                after.present    = true;
            }
            after.components[column.component] = value;
        }
        values.push_back(after);
    }
    std::string error;
    const std::shared_ptr<Operation> operation = make_geometry_attribute_operation(m_context, mesh, m_primitive_index, attribute, elements, values, error);
    if (!operation) {
        log_operations->warn("Geometry Spreadsheet: {}", error);
        return;
    }
    m_context.operation_stack->queue(operation);
}

} // namespace editor
