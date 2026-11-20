#include "level/kit_layout_editor.hpp"

#include "level/kit_parameter_rows.hpp"
#include "level/kit_shape_library.hpp"
#include "level/level_kits.hpp"
#include "level/level_queries.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "brushes/brush_tool.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_imgui/imgui_item_recorder.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h> // ImGui::GetActiveID(): the open edit's widget

#include <algorithm>
#include <array>
#include <optional>

namespace editor {

namespace {

void tooltip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text);
    }
}

// The last item's edit: changed this frame (a drag step, a typed character), ended with a change, or ended without one.
[[nodiscard]] auto drag_edit(const bool changed) -> Kit_row_edit
{
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        return Kit_row_edit::committed;
    }
    if (ImGui::IsItemDeactivated()) {
        return Kit_row_edit::cancelled;
    }
    return changed ? Kit_row_edit::changed : Kit_row_edit::none;
}

[[nodiscard]] auto pressed_edit(const bool pressed) -> Kit_row_edit
{
    return pressed ? Kit_row_edit::committed : Kit_row_edit::none;
}

// Names the widget submitted last within a row of several (Properties: '<row>.<role>'); beside, its own label names it.
void name_role(const Kit_row_label label, const char* role)
{
    if (label == Kit_row_label::recorded) {
        erhe::imgui::set_item_debug_role(role);
    }
}

// The widget label: the name beside the widget, or a hidden one that takes the row's label (Properties).
[[nodiscard]] auto widget_label(const char* name, const Kit_row_label label) -> const char*
{
    return (label == Kit_row_label::beside) ? name : "##";
}

using enum Kit_layout_row;
constexpr std::array<Kit_layout_row, 5> c_line_count_rows{kind, count_mode, count, direction, step};
constexpr std::array<Kit_layout_row, 5> c_line_gap_rows  {kind, count_mode, gap,   direction, length};
constexpr std::array<Kit_layout_row, 5> c_grid_rows      {kind, count_x, count_z, spacing_x, spacing_z};
constexpr std::array<Kit_layout_row, 6> c_ring_count_rows{kind, count_mode, count, radius, arc, facing};
constexpr std::array<Kit_layout_row, 6> c_ring_gap_rows  {kind, count_mode, gap,   radius, arc, facing};
constexpr std::array<Kit_layout_row, 4> c_path_count_rows{kind, count_mode, count, orientation};
constexpr std::array<Kit_layout_row, 4> c_path_gap_rows  {kind, count_mode, gap,   orientation};
constexpr std::array<Kit_layout_row, 2> c_segments_rows  {kind, orientation};
constexpr std::array<Kit_layout_row, 7> c_stairs_rows    {kind, stairs_width, stairs_rise, stairs_tread, stairs_thickness, stairs_tolerance, stairs_material};
constexpr std::array<Kit_layout_row, 7> c_floor_rows     {kind, floor_thickness, floor_wall_thickness, floor_room_height, floor_rail_inset, floor_material, floor_wall_material};
constexpr std::array<Kit_layout_row, 9> c_scatter_rectangle_count_rows  {kind, scatter_mode, count,   region, size_x, size_z, min_spacing, yaw_min, yaw_max};
constexpr std::array<Kit_layout_row, 9> c_scatter_rectangle_density_rows{kind, scatter_mode, density, region, size_x, size_z, min_spacing, yaw_min, yaw_max};
constexpr std::array<Kit_layout_row, 8> c_scatter_disc_count_rows       {kind, scatter_mode, count,   region, region_radius, min_spacing, yaw_min, yaw_max};
constexpr std::array<Kit_layout_row, 8> c_scatter_disc_density_rows     {kind, scatter_mode, density, region, region_radius, min_spacing, yaw_min, yaw_max};

[[nodiscard]] auto get_scatter_rows(const Kit_layout_pattern& pattern) -> std::span<const Kit_layout_row>
{
    const bool density_mode = (pattern.scatter_mode == Kit_scatter_mode::density);
    if (pattern.region == Repeat_scatter_region::disc) {
        return density_mode ? std::span<const Kit_layout_row>{c_scatter_disc_density_rows} : std::span<const Kit_layout_row>{c_scatter_disc_count_rows};
    }
    return density_mode ? std::span<const Kit_layout_row>{c_scatter_rectangle_density_rows} : std::span<const Kit_layout_row>{c_scatter_rectangle_count_rows};
}

[[nodiscard]] auto kind_radio(int& kind, const Repeat_pattern_kind value, const char* name, const Kit_row_label label, const char* text) -> bool
{
    const bool pressed = ImGui::RadioButton(name, &kind, static_cast<int>(value));
    name_role(label, c_str(value));
    tooltip(text);
    return pressed;
}

[[nodiscard]] auto kind_row(Kit_layout_pattern& pattern, const Kit_layout_kinds kinds, const Kit_row_label label) -> bool
{
    int  kind  = static_cast<int>(pattern.kind);
    bool ended = false;
    if (kinds != Kit_layout_kinds::curve) {
        ended = kind_radio(kind, Repeat_pattern_kind::line, "Line", label, "Pieces along a direction from the layout node's origin.") || ended;
        ImGui::SameLine();
        ended = kind_radio(kind, Repeat_pattern_kind::grid, "Grid", label, "Count X by Count Z pieces along the node's x and z axes.") || ended;
        ImGui::SameLine();
        ended = kind_radio(kind, Repeat_pattern_kind::ring, "Ring", label, "Pieces on a ring about the node's up axis, its center Radius behind the node's front.") || ended;
        ImGui::SameLine();
        ended = kind_radio(kind, Repeat_pattern_kind::scatter, "Scatter", label, "Pieces at seeded random points of a region centred on the node's origin, each item picked by its weight, their footprints at least Min Spacing apart; Surface Snap snaps them to the ground.") || ended;
    }
    if (kinds == Kit_layout_kinds::all) {
        ImGui::SameLine();
    }
    if (kinds != Kit_layout_kinds::xform) {
        ended = kind_radio(kind, Repeat_pattern_kind::path, "Path", label,
            (kinds == Kit_layout_kinds::curve)
                ? "This layout node is a curve: its pieces follow its own points. A line, grid, ring or scatter layout is an Xform node."
                : "Pieces along the layout node's own curve by distance; the new node is a curve node, a polyline a curve of corner points.") || ended;
        ImGui::SameLine();
        ended = kind_radio(kind, Repeat_pattern_kind::segments, "Segments", label,
            "One piece per segment between two neighbouring points of the layout node's own curve, centred on the straight chord and "
            "taking its length as the piece's length (walls between room corners); handles do not bend the pieces.") || ended;
        ImGui::SameLine();
        ended = kind_radio(kind, Repeat_pattern_kind::stairs, "Stairs", label,
            "A staircase generated along the layout node's own curve: flights of equal steps and level landings that stay within one step rise "
            "of the curve's height, its side edges following the curve in plan.") || ended;
        ImGui::SameLine();
        ended = kind_radio(kind, Repeat_pattern_kind::floor, "Floor", label,
            "A floor slab generated to fill the layout node's own closed curve of corner points; walls stand on its edges' wall "
            "connectors.") || ended;
    }
    if (kinds == Kit_layout_kinds::xform) {
        ImGui::SameLine();
        ImGui::TextDisabled("(Path, Segments, Stairs: a curve node)");
        tooltip("A path, segments or stairs layout is a curve node carrying the layout values; this node is an Xform, so it takes a line, grid, ring or scatter.");
    }
    pattern.kind = static_cast<Repeat_pattern_kind>(kind);
    return ended;
}

[[nodiscard]] auto to_surface_snap  (const bool on) -> Surface_snap   { return on ? Surface_snap::on : Surface_snap::off; }
[[nodiscard]] auto to_surface_orient(const bool on) -> Surface_orient { return on ? Surface_orient::on : Surface_orient::off; }

[[nodiscard]] auto get_surface_support_label(const Surface_support support) -> const char*
{
    switch (support) {
        case Surface_support::five_lines:      return "Five Lines";
        case Surface_support::center_blocks:   return "Center Blocks";
        case Surface_support::center_fallback: return "Center Fallback";
    }
    return "?";
}

[[nodiscard]] auto get_kinds(const erhe::scene::Node& node) -> Kit_layout_kinds
{
    return (dynamic_cast<const erhe::scene::Curve*>(&node) != nullptr) ? Kit_layout_kinds::curve : Kit_layout_kinds::xform;
}

} // anonymous namespace

auto get_layout_rows(const Kit_layout_pattern& pattern) -> std::span<const Kit_layout_row>
{
    const bool gap_mode = (pattern.count_mode == Kit_layout_count_mode::gap);
    switch (pattern.kind) {
        case Repeat_pattern_kind::line: return gap_mode ? std::span<const Kit_layout_row>{c_line_gap_rows} : std::span<const Kit_layout_row>{c_line_count_rows};
        case Repeat_pattern_kind::grid: return c_grid_rows;
        case Repeat_pattern_kind::ring: return gap_mode ? std::span<const Kit_layout_row>{c_ring_gap_rows} : std::span<const Kit_layout_row>{c_ring_count_rows};
        case Repeat_pattern_kind::path: return gap_mode ? std::span<const Kit_layout_row>{c_path_gap_rows} : std::span<const Kit_layout_row>{c_path_count_rows};
        case Repeat_pattern_kind::scatter: return get_scatter_rows(pattern);
        case Repeat_pattern_kind::segments: return c_segments_rows;
        case Repeat_pattern_kind::stairs:   return c_stairs_rows;
        case Repeat_pattern_kind::floor:    return c_floor_rows;
        default:                        return {};
    }
}

auto get_layout_row_name(const Kit_layout_row row) -> const char*
{
    switch (row) {
        case Kit_layout_row::kind:        return "Kind";
        case Kit_layout_row::count_mode:  return "Count Mode";
        case Kit_layout_row::count:       return "Count";
        case Kit_layout_row::gap:         return "Gap";
        case Kit_layout_row::direction:   return "Direction";
        case Kit_layout_row::step:        return "Step";
        case Kit_layout_row::length:      return "Length";
        case Kit_layout_row::count_x:     return "Count X";
        case Kit_layout_row::count_z:     return "Count Z";
        case Kit_layout_row::spacing_x:   return "Spacing X";
        case Kit_layout_row::spacing_z:   return "Spacing Z";
        case Kit_layout_row::radius:      return "Radius";
        case Kit_layout_row::arc:         return "Arc";
        case Kit_layout_row::facing:      return "Face Outward";
        case Kit_layout_row::orientation: return "Orientation";
        case Kit_layout_row::scatter_mode:  return "Scatter Mode";
        case Kit_layout_row::density:       return "Density";
        case Kit_layout_row::region:        return "Region";
        case Kit_layout_row::size_x:        return "Size X";
        case Kit_layout_row::size_z:        return "Size Z";
        case Kit_layout_row::region_radius: return "Region Radius";
        case Kit_layout_row::min_spacing:   return "Min Spacing";
        case Kit_layout_row::yaw_min:       return "Yaw Min";
        case Kit_layout_row::yaw_max:       return "Yaw Max";
        case Kit_layout_row::stairs_width:     return "Width";
        case Kit_layout_row::stairs_rise:      return "Step Rise Limit";
        case Kit_layout_row::stairs_tread:     return "Tread Depth";
        case Kit_layout_row::stairs_thickness: return "Thickness";
        case Kit_layout_row::stairs_tolerance: return "Edge Tolerance";
        case Kit_layout_row::stairs_material:  return "Material";
        case Kit_layout_row::floor_thickness:      return "Thickness";
        case Kit_layout_row::floor_wall_thickness: return "Wall Thickness";
        case Kit_layout_row::floor_room_height:    return "Room Height";
        case Kit_layout_row::floor_material:       return "Material";
        case Kit_layout_row::floor_wall_material:  return "Wall Material";
        case Kit_layout_row::floor_rail_inset:     return "Rail Inset";
        default:                          return "?";
    }
}

auto get_layout_setting_name(const Kit_layout_setting_row row) -> const char*
{
    switch (row) {
        case Kit_layout_setting_row::module_snap:    return "Module Snap";
        case Kit_layout_setting_row::surface_snap:   return "Surface Snap";
        case Kit_layout_setting_row::surface_orient: return "Surface Orient";
        case Kit_layout_setting_row::per_piece_seed: return "Per Piece Seed";
        case Kit_layout_setting_row::seed:           return "Layout Seed";
        case Kit_layout_setting_row::surface_direction:         return "Surface Direction";
        case Kit_layout_setting_row::surface_forward_distance:  return "Surface Forward Distance";
        case Kit_layout_setting_row::surface_backward_distance: return "Surface Backward Distance";
        case Kit_layout_setting_row::surface_support:           return "Surface Support";
        case Kit_layout_setting_row::hide_all_connectors:            return "Hide All Connectors";
        case Kit_layout_setting_row::hide_plus_tangent_connectors:   return "Hide +Tangent Connectors";
        case Kit_layout_setting_row::hide_minus_tangent_connectors:  return "Hide -Tangent Connectors";
        case Kit_layout_setting_row::hide_plus_normal_connectors:    return "Hide +Normal Connectors";
        case Kit_layout_setting_row::hide_minus_normal_connectors:   return "Hide -Normal Connectors";
        case Kit_layout_setting_row::hide_plus_binormal_connectors:  return "Hide +Binormal Connectors";
        case Kit_layout_setting_row::hide_minus_binormal_connectors: return "Hide -Binormal Connectors";
        default:                                     return "?";
    }
}

auto get_layout_setting_rows(const Kit_layout_pattern& pattern) -> std::span<const Kit_layout_setting_row>
{
    static constexpr std::array<Kit_layout_setting_row, 16> rows{
        Kit_layout_setting_row::module_snap, Kit_layout_setting_row::surface_snap, Kit_layout_setting_row::surface_direction,
        Kit_layout_setting_row::surface_forward_distance, Kit_layout_setting_row::surface_backward_distance, Kit_layout_setting_row::surface_support,
        Kit_layout_setting_row::surface_orient, Kit_layout_setting_row::per_piece_seed, Kit_layout_setting_row::seed,
        Kit_layout_setting_row::hide_all_connectors,
        Kit_layout_setting_row::hide_plus_tangent_connectors,  Kit_layout_setting_row::hide_minus_tangent_connectors,
        Kit_layout_setting_row::hide_plus_normal_connectors,   Kit_layout_setting_row::hide_minus_normal_connectors,
        Kit_layout_setting_row::hide_plus_binormal_connectors, Kit_layout_setting_row::hide_minus_binormal_connectors
    };
    constexpr std::size_t c_rows_of_every_kind = 9; // the connector switches follow
    const std::span<const Kit_layout_setting_row> all{rows};
    // A stairs or floor layout places no piece: nothing snaps, takes a seed or has connectors.
    if ((pattern.kind == Repeat_pattern_kind::stairs) || (pattern.kind == Repeat_pattern_kind::floor)) {
        return {};
    }
    return is_curve_layout_kind(pattern.kind) ? all : all.first(c_rows_of_every_kind);
}

namespace {

[[nodiscard]] auto get_hide_row_direction(const Kit_layout_setting_row row) -> Kit_layout_direction
{
    switch (row) {
        case Kit_layout_setting_row::hide_plus_tangent_connectors:   return Kit_layout_direction::plus_tangent;
        case Kit_layout_setting_row::hide_minus_tangent_connectors:  return Kit_layout_direction::minus_tangent;
        case Kit_layout_setting_row::hide_plus_normal_connectors:    return Kit_layout_direction::plus_normal;
        case Kit_layout_setting_row::hide_minus_normal_connectors:   return Kit_layout_direction::minus_normal;
        case Kit_layout_setting_row::hide_plus_binormal_connectors:  return Kit_layout_direction::plus_binormal;
        case Kit_layout_setting_row::hide_minus_binormal_connectors:
        default:                                                     return Kit_layout_direction::minus_binormal;
    }
}

[[nodiscard]] auto get_hide_row_tooltip(const Kit_layout_direction direction) -> const char*
{
    switch (direction) {
        case Kit_layout_direction::plus_tangent:  return "Hides the children's connectors facing along the curve (each piece's +x); an open curve's first piece hides only this one of its six directions.";
        case Kit_layout_direction::minus_tangent: return "Hides the children's connectors facing back along the curve (each piece's -x); an open curve's last piece hides only this one of its six directions.";
        case Kit_layout_direction::plus_normal:   return "Hides the inner children's connectors facing the right of the curve's direction (each piece's +z).";
        case Kit_layout_direction::minus_normal:  return "Hides the inner children's connectors facing the left of the curve's direction (each piece's -z).";
        case Kit_layout_direction::plus_binormal: return "Hides the inner children's connectors facing up (each piece's +y).";
        case Kit_layout_direction::minus_binormal:
        default:                                  return "Hides the inner children's connectors facing down (each piece's -y).";
    }
}

} // anonymous namespace

auto layout_pattern_row(
    Kit_layout_pattern& pattern, const Kit_layout_row row, const Kit_layout_kinds kinds, const Kit_row_label label, const Kit_shape_library& library
) -> Kit_row_edit
{
    const int    max_count = static_cast<int>(c_max_repeat_count);
    const char*  name      = widget_label(get_layout_row_name(row), label);
    bool         ended     = false; // a radio button or checkbox
    Kit_row_edit edit      = Kit_row_edit::none;
    // A combo of a slot's default and alternatives over a material name (empty: the default); views only, no copy per frame.
    const auto material_combo = [&ended](const char* const combo_name, std::string& value, const Kit_slot_palette* const palette) {
        const char* const shown_text = !value.empty() ? value.c_str() : ((palette != nullptr) ? palette->default_material.c_str() : "(default)");
        if (ImGui::BeginCombo(combo_name, shown_text)) {
            const std::string shown{shown_text}; // while the combo is open: the selection below replaces the viewed name
            const auto option = [&value, &ended, &shown](const std::string& material) {
                if (ImGui::Selectable(material.c_str(), material == shown) && (material != shown)) {
                    value = material;
                    ended = true;
                }
            };
            if (palette != nullptr) {
                option(palette->default_material);
                for (const std::string& alternative : palette->alternatives) {
                    option(alternative);
                }
            }
            ImGui::EndCombo();
        }
    };
    switch (row) {
        case Kit_layout_row::kind: {
            ended = kind_row(pattern, kinds, label);
            break;
        }
        case Kit_layout_row::count_mode: {
            int mode = static_cast<int>(pattern.count_mode);
            ended = ImGui::RadioButton("By Count", &mode, static_cast<int>(Kit_layout_count_mode::count)) || ended;
            name_role(label, "count");
            tooltip("The piece count is stated.");
            ImGui::SameLine();
            ended = ImGui::RadioButton("By Gap", &mode, static_cast<int>(Kit_layout_count_mode::gap)) || ended;
            name_role(label, "gap");
            tooltip("The free distance between neighbouring pieces is stated; the space of the arrangement gives the count.");
            pattern.count_mode = static_cast<Kit_layout_count_mode>(mode);
            break;
        }
        case Kit_layout_row::count: {
            const bool changed = ImGui::DragInt(name, &pattern.count, 0.1f, 1, max_count, "%d", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("Pieces in the run.");
            break;
        }
        case Kit_layout_row::gap: {
            const bool changed = ImGui::DragFloat(name, &pattern.gap, 0.01f, -100.0f, 100.0f, "%.3f m");
            edit = drag_edit(changed);
            tooltip("The least free distance between neighbouring pieces; the remainder of the space spreads over the gaps.");
            break;
        }
        case Kit_layout_row::direction: {
            const std::size_t first_item = erhe::imgui::get_recorded_item_count();
            const bool changed = ImGui::DragFloat3(name, &pattern.direction.x, 0.01f, -1.0f, 1.0f, "%.3f");
            edit = drag_edit(changed);
            // The components are recorded as 'Direction.x', '.y' and '.z'.
            if ((label == Kit_row_label::beside) && erhe::imgui::is_item_recording()) {
                erhe::imgui::set_recorded_item_labels(first_item, "Direction");
            }
            tooltip("The line's direction in the node's frame.");
            break;
        }
        case Kit_layout_row::step: {
            const bool changed = ImGui::DragFloat(name, &pattern.step, 0.01f, 0.0f, c_max_line_step, "%.3f m");
            edit = drag_edit(changed);
            tooltip("Center to center; by default the piece's width along x.");
            break;
        }
        case Kit_layout_row::length: {
            const bool changed = ImGui::DragFloat(name, &pattern.length, 0.01f, 0.0f, c_max_line_length, "%.3f m");
            edit = drag_edit(changed);
            tooltip("From the first piece's center to the last one's.");
            break;
        }
        case Kit_layout_row::count_x: {
            const bool changed = ImGui::DragInt(name, &pattern.count_x, 0.1f, 1, max_count, "%d", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("Pieces along the node's x axis.");
            break;
        }
        case Kit_layout_row::count_z: {
            const bool changed = ImGui::DragInt(name, &pattern.count_z, 0.1f, 1, max_count, "%d", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("Pieces along the node's z axis.");
            break;
        }
        case Kit_layout_row::spacing_x: {
            const bool changed = ImGui::DragFloat(name, &pattern.spacing_x, 0.01f, -c_max_grid_spacing, c_max_grid_spacing, "%.3f m");
            edit = drag_edit(changed);
            tooltip("The step along the node's x axis; by default the piece's extent along it.");
            break;
        }
        case Kit_layout_row::spacing_z: {
            const bool changed = ImGui::DragFloat(name, &pattern.spacing_z, 0.01f, -c_max_grid_spacing, c_max_grid_spacing, "%.3f m");
            edit = drag_edit(changed);
            tooltip("The step along the node's z axis; by default the piece's extent along it.");
            break;
        }
        case Kit_layout_row::radius: {
            const bool changed = ImGui::DragFloat(name, &pattern.radius, 0.01f, c_min_ring_radius, c_max_ring_radius, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            break;
        }
        case Kit_layout_row::arc: {
            const bool changed = ImGui::DragFloat(name, &pattern.arc_degrees, 0.5f, c_min_ring_arc, c_max_ring_arc, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("360 is a full ring; a smaller arc has pieces at both of its ends.");
            break;
        }
        case Kit_layout_row::facing: {
            bool outward = (pattern.facing == Repeat_ring_facing::outward);
            if (ImGui::Checkbox(name, &outward)) {
                pattern.facing = outward ? Repeat_ring_facing::outward : Repeat_ring_facing::start_rotation;
                ended = true;
            }
            tooltip("Each piece turns with the ring; off, each keeps the node's rotation.");
            break;
        }
        case Kit_layout_row::orientation: {
            int orientation = static_cast<int>(pattern.orientation);
            ended = ImGui::RadioButton("Upright", &orientation, static_cast<int>(Repeat_path_orientation::upright)) || ended;
            name_role(label, "upright");
            tooltip("Each piece turns about the node's up axis to the curve's heading and stays upright.");
            ImGui::SameLine();
            ended = ImGui::RadioButton("Follow Tangent", &orientation, static_cast<int>(Repeat_path_orientation::tangent)) || ended;
            name_role(label, "tangent");
            tooltip("Each piece's x axis takes the curve's tangent: it pitches with the curve.");
            pattern.orientation = static_cast<Repeat_path_orientation>(orientation);
            break;
        }
        case Kit_layout_row::scatter_mode: {
            int mode = static_cast<int>(pattern.scatter_mode);
            ended = ImGui::RadioButton("By Count", &mode, static_cast<int>(Kit_scatter_mode::count)) || ended;
            name_role(label, "count");
            tooltip("The piece count is stated.");
            ImGui::SameLine();
            ended = ImGui::RadioButton("By Density", &mode, static_cast<int>(Kit_scatter_mode::density)) || ended;
            name_role(label, "density");
            tooltip("Pieces per square meter are stated; the region's area gives the count.");
            pattern.scatter_mode = static_cast<Kit_scatter_mode>(mode);
            break;
        }
        case Kit_layout_row::density: {
            const bool changed = ImGui::DragFloat(name, &pattern.density, 0.001f, 0.0001f, 100.0f, "%.4f /m2", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("Pieces per square meter of the region; fewer are placed when no more fit the spacing.");
            break;
        }
        case Kit_layout_row::region: {
            int region = static_cast<int>(pattern.region);
            ended = ImGui::RadioButton("Rectangle", &region, static_cast<int>(Repeat_scatter_region::rectangle)) || ended;
            name_role(label, "rectangle");
            tooltip("Size X by Size Z, centred on the node's origin.");
            ImGui::SameLine();
            ended = ImGui::RadioButton("Disc", &region, static_cast<int>(Repeat_scatter_region::disc)) || ended;
            name_role(label, "disc");
            tooltip("Region Radius about the node's origin.");
            pattern.region = static_cast<Repeat_scatter_region>(region);
            break;
        }
        case Kit_layout_row::size_x: {
            const bool changed = ImGui::DragFloat(name, &pattern.size_x, 0.01f, c_min_scatter_region_size, c_max_scatter_region_size, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("The region's extent along the node's x axis.");
            break;
        }
        case Kit_layout_row::size_z: {
            const bool changed = ImGui::DragFloat(name, &pattern.size_z, 0.01f, c_min_scatter_region_size, c_max_scatter_region_size, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("The region's extent along the node's z axis.");
            break;
        }
        case Kit_layout_row::region_radius: {
            const bool changed = ImGui::DragFloat(name, &pattern.region_radius, 0.01f, c_min_scatter_region_size, c_max_scatter_region_size, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("The disc's radius about the node's origin.");
            break;
        }
        case Kit_layout_row::min_spacing: {
            const bool changed = ImGui::DragFloat(name, &pattern.min_spacing, 0.01f, 0.0f, 1000.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("The least gap between two pieces' footprints in the node's xz plane: each footprint is the circle about the piece's vertical axis around its bounds.");
            break;
        }
        case Kit_layout_row::yaw_min: {
            const bool changed = ImGui::DragFloat(name, &pattern.yaw_min, 0.5f, -360.0f, pattern.yaw_max, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("Each piece turns about the node's up axis by a seeded angle from Yaw Min to Yaw Max.");
            break;
        }
        case Kit_layout_row::yaw_max: {
            const bool changed = ImGui::DragFloat(name, &pattern.yaw_max, 0.5f, pattern.yaw_min, 360.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("Each piece turns about the node's up axis by a seeded angle from Yaw Min to Yaw Max.");
            break;
        }
        case Kit_layout_row::stairs_width: {
            const bool changed = ImGui::DragFloat(name, &pattern.stairs.width, 0.01f, c_min_stairs_width, c_max_stairs_width, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("The stairs' width across the curve; the curve's plan radius must be at least 0.75 times it.");
            break;
        }
        case Kit_layout_row::stairs_rise: {
            const bool changed = ImGui::DragFloat(name, &pattern.stairs.max_rise, 0.005f, c_min_stairs_rise, c_max_stairs_rise, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("The highest allowed single step; each climb takes the least count of equal steps within it, and the walking surface stays within it of the curve.");
            break;
        }
        case Kit_layout_row::stairs_tread: {
            const bool changed = ImGui::DragFloat(name, &pattern.stairs.min_tread, 0.005f, c_min_stairs_tread, c_max_stairs_tread, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("The depth of a flight's treads, measured along the curve; a landing is deeper. With the rise limit it sets the steepest curve the stairs follow.");
            break;
        }
        case Kit_layout_row::stairs_thickness: {
            const bool changed = ImGui::DragFloat(name, &pattern.stairs.thickness, 0.005f, c_min_stairs_thickness, c_max_stairs_thickness, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("The underside lies this far below a landing and below the foot of each riser, sloping under a flight.");
            break;
        }
        case Kit_layout_row::stairs_tolerance: {
            const bool changed = ImGui::DragFloat(name, &pattern.stairs.tolerance, 0.001f, c_min_stairs_tolerance, c_max_stairs_tolerance, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("The most a straight piece of a curved side edge leaves its curve: smaller makes smoother edges of more triangles. Straight parts take none.");
            break;
        }
        case Kit_layout_row::stairs_material: {
            material_combo(name, pattern.stairs_material, library.find_slot_palette(c_stairs_material_slot));
            tooltip("The staircase's material: the stone slot's default first, then its alternatives.");
            break;
        }
        case Kit_layout_row::floor_thickness: {
            const bool changed = ImGui::DragFloat(name, &pattern.floor.thickness, 0.005f, 0.01f, 4.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("The slab's thickness: its top, where walls stand, is this far above the node's origin.");
            break;
        }
        case Kit_layout_row::floor_wall_thickness: {
            const bool changed = ImGui::DragFloat(name, &pattern.floor.wall_thickness, 0.005f, 0.0f, 4.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("The thickness of the walls on the floor's edges: the wall connectors sit half of it inside each edge.");
            break;
        }
        case Kit_layout_row::floor_room_height: {
            const bool changed = ImGui::DragFloat(name, &pattern.floor.room_height, 0.01f, 0.1f, 32.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("The height of the walls standing on the floor.");
            break;
        }
        case Kit_layout_row::floor_rail_inset: {
            const bool changed = ImGui::DragFloat(name, &pattern.floor.rail_inset, 0.005f, 0.0f, 4.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            edit = drag_edit(changed);
            tooltip("Riders (railings, battlements) on the floor's edges stand on a line this far inside each edge; riders on neighbouring edges meet where their lines cross.");
            break;
        }
        case Kit_layout_row::floor_material: {
            material_combo(name, pattern.floor_material, library.find_slot_palette(c_floor_material_slot));
            tooltip("The slab's material: the floor slot's default first, then its alternatives.");
            break;
        }
        case Kit_layout_row::floor_wall_material: {
            material_combo(name, pattern.floor_wall_material, library.find_slot_palette(c_floor_wall_material_slot));
            tooltip("The material of the walls standing on the floor: the wall slot's default first, then its alternatives.");
            break;
        }
        default: {
            break;
        }
    }
    return merge(edit, pressed_edit(ended));
}

auto layout_setting_row(Kit_layout_data& data, const Kit_layout_setting_row row, const Kit_row_label label) -> Kit_row_edit
{
    const char* name = widget_label(get_layout_setting_name(row), label);
    switch (row) {
        case Kit_layout_setting_row::module_snap: {
            bool value = (data.snap == Kit_snap::module);
            const bool pressed = ImGui::Checkbox(name, &value);
            tooltip("Each piece's position rounds to multiples of the scene's module.");
            data.snap = value ? Kit_snap::module : Kit_snap::none;
            return pressed_edit(pressed);
        }
        case Kit_layout_setting_row::surface_snap: {
            bool value = (data.surface_snap == Surface_snap::on);
            const bool pressed = ImGui::Checkbox(name, &value);
            tooltip("Each piece snaps to the surface under it.");
            data.surface_snap = to_surface_snap(value);
            return pressed_edit(pressed);
        }
        case Kit_layout_setting_row::surface_orient: {
            bool value = (data.surface_orient == Surface_orient::on);
            const bool pressed = ImGui::Checkbox(name, &value);
            tooltip("Each piece's up axis takes the normal of the surface under it.");
            data.surface_orient = to_surface_orient(value);
            return pressed_edit(pressed);
        }
        case Kit_layout_setting_row::surface_direction: {
            const bool changed = ImGui::DragFloat3(name, &data.surface_direction.x, 0.01f, -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            const Kit_row_edit edit = drag_edit(changed);
            tooltip("World space: the direction each piece searches along for the surface it snaps to, normalized; (0, -1, 0) is down, (0, 1, 0) a ceiling. Not zero.");
            return edit;
        }
        case Kit_layout_setting_row::surface_forward_distance: {
            const bool changed = ImGui::DragFloat(name, &data.surface_forward_distance, 0.01f, 0.0f, 1000.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
            const Kit_row_edit edit = drag_edit(changed);
            tooltip("Meters: each piece moves at most this far along the search direction to the nearest place where it rests and fits; beyond, it stays where the pattern puts it.");
            return edit;
        }
        case Kit_layout_setting_row::surface_backward_distance: {
            const bool changed = ImGui::DragFloat(name, &data.surface_backward_distance, 0.01f, 0.0f, 1000.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
            const Kit_row_edit edit = drag_edit(changed);
            tooltip("Meters: each piece moves at most this far against the search direction; above 0 the nearest place either way is taken, 0 searches along the direction only.");
            return edit;
        }
        case Kit_layout_setting_row::surface_support: {
            constexpr std::array<Surface_support, 3> c_supports{Surface_support::five_lines, Surface_support::center_blocks, Surface_support::center_fallback};
            bool pressed = false;
            if (ImGui::BeginCombo(name, get_surface_support_label(data.surface_support))) {
                for (const Surface_support support : c_supports) {
                    if (ImGui::Selectable(get_surface_support_label(support), support == data.surface_support)) {
                        pressed = (support != data.surface_support);
                        data.surface_support = support;
                    }
                }
                ImGui::EndCombo();
            }
            tooltip(
                "Which base lines support and block each piece's snap without orient. Five Lines: the base center and the four "
                "footprint corners rest on a surface, and a face inside the piece on any of them refuses the place. Center Blocks: "
                "the five lines rest, only a face on the center line refuses. Center Fallback: the nearest place, and when a face "
                "refuses it, the piece rests on the surface under its base center, along the search direction, without a fit test."
            );
            return pressed_edit(pressed);
        }
        case Kit_layout_setting_row::per_piece_seed: {
            bool value = (data.seeds == Kit_layout_seeds::per_element);
            const bool pressed = ImGui::Checkbox(name, &value);
            tooltip("Each piece takes a seed of its own from the layout seed and its index; its item's seed mask derives from it.");
            data.seeds = value ? Kit_layout_seeds::per_element : Kit_layout_seeds::item;
            return pressed_edit(pressed);
        }
        case Kit_layout_setting_row::seed: {
            const bool changed = ImGui::DragInt(name, &data.seed, 1.0f, 0, kit_seed_max, "%d", ImGuiSliderFlags_AlwaysClamp);
            const Kit_row_edit edit = drag_edit(changed);
            tooltip("The seed the per-piece seeds and a scatter's points come from.");
            return edit;
        }
        case Kit_layout_setting_row::hide_all_connectors: {
            bool value = (data.connector_hiding.all == Kit_layout_connector_visibility::hidden);
            const bool pressed = ImGui::Checkbox(name, &value);
            tooltip(
                "Hides the connectors of the children of a closed curve and of an open curve's inner children; an open curve's "
                "first and last piece hide only the connectors facing their neighbour. Hidden connectors are not drawn, snapped to or joined."
            );
            data.connector_hiding.all = value ? Kit_layout_connector_visibility::hidden : Kit_layout_connector_visibility::shown;
            return pressed_edit(pressed);
        }
        case Kit_layout_setting_row::hide_plus_tangent_connectors:
        case Kit_layout_setting_row::hide_minus_tangent_connectors:
        case Kit_layout_setting_row::hide_plus_normal_connectors:
        case Kit_layout_setting_row::hide_minus_normal_connectors:
        case Kit_layout_setting_row::hide_plus_binormal_connectors:
        case Kit_layout_setting_row::hide_minus_binormal_connectors: {
            const Kit_layout_direction direction = get_hide_row_direction(row);
            bool value = data.connector_hiding.is_hidden(direction);
            // Hide All hides every direction: the six keep their values and take effect when it is off.
            ImGui::BeginDisabled(data.connector_hiding.all == Kit_layout_connector_visibility::hidden);
            const bool pressed = ImGui::Checkbox(name, &value);
            ImGui::EndDisabled();
            tooltip(get_hide_row_tooltip(direction));
            data.connector_hiding.set(direction, value ? Kit_layout_connector_visibility::hidden : Kit_layout_connector_visibility::shown);
            return pressed_edit(pressed);
        }
        default: {
            return Kit_row_edit::none;
        }
    }
}

Kit_layout_editor::Kit_layout_editor(App_context& context)
    : m_context{context}
{
}

void Kit_layout_editor::mark_dirty()
{
    m_dirty = true;
}

void Kit_layout_editor::on_curve_changed(const erhe::scene::Curve& curve)
{
    if (m_layout.lock().get() == static_cast<const erhe::scene::Node*>(&curve)) {
        m_dirty = true;
    }
}

void Kit_layout_editor::update(const std::shared_ptr<erhe::scene::Node>& layout)
{
    if (m_layout.lock() != layout) {
        m_layout  = layout;
        m_ready   = false;
        m_dirty   = true;
        m_message.clear();
        m_failed  = false;
        Scene_root* const scene_root = layout ? dynamic_cast<Scene_root*>(layout->get_item_host()) : nullptr;
        m_scene_root = (scene_root != nullptr) ? scene_root->shared_from_this() : std::shared_ptr<Scene_root>{};
    }
    // Read again only on messages or another node, never while one of the rows is being edited.
    if (layout && m_dirty && !m_read_queued && !ImGui::IsAnyItemActive()) {
        queue_read();
    }
}

auto Kit_layout_editor::is_ready() const -> bool
{
    return m_ready;
}

auto Kit_layout_editor::get_problem() const -> const std::string&
{
    return m_problem;
}

auto Kit_layout_editor::get_pattern() const -> const Kit_layout_pattern&
{
    return m_data.pattern;
}

auto Kit_layout_editor::get_items() const -> std::span<const Kit_layout_item_rows>
{
    return m_items;
}

auto Kit_layout_editor::get_message() const -> const std::string&
{
    return m_message;
}

void Kit_layout_editor::state_line()
{
    const std::shared_ptr<erhe::scene::Node> layout = m_layout.lock();
    const bool locked = (m_data.lock == Kit_layout_lock::locked);
    ImGui::Text("%s, %zu pieces", locked ? "Locked" : "Unlocked", layout ? layout->get_child_count() : std::size_t{0});
    tooltip(
        locked
            ? "Locked: the layout makes its children from these values; selecting a piece selects the layout node."
            : "Unlocked: the children are edited directly; these rows change the values alone."
    );
}

void Kit_layout_editor::pattern_row(const Kit_layout_row row)
{
    const Kit_row_edit edit = layout_pattern_row(m_data.pattern, row, m_kinds, Kit_row_label::recorded, m_context.level_kits->get_library());
    on_row_edit(edit, m_data);
}

void Kit_layout_editor::run_line()
{
    if (!m_run_problem.empty()) {
        kit_message_line(m_run_problem);
    } else if (m_data.pattern.kind == Repeat_pattern_kind::stairs) {
        const Stairs_fit& fit = m_stairs.fit;
        ImGui::Text(
            "%d steps in %zu flights, %zu landings; rise %.3f m over %.3f m", fit.step_count, fit.flights.size(), fit.landings.size(), fit.total_rise, fit.length
        );
        tooltip("The steps, the flights of equal steps and the level landings the curve takes, its end height minus its start height and its length in plan.");
        ImGui::Text("within %.3f m of the curve", fit.max_deviation);
        tooltip("The largest height difference between the walking surface and the curve; at most the step rise limit.");
    } else if (m_data.pattern.kind == Repeat_pattern_kind::grid) {
        ImGui::Text("%zu pieces", m_run.count);
    } else if (m_data.pattern.kind == Repeat_pattern_kind::scatter) {
        if (m_run.count < m_run.requested) {
            ImGui::Text("%zu of %zu pieces fit over %.3f m2", m_run.count, m_run.requested, m_run.space);
            tooltip("No more pieces fit the region with Min Spacing between their footprints; the scatter places the ones that do.");
        } else {
            ImGui::Text("%zu pieces over %.3f m2", m_run.count, m_run.space);
        }
    } else if (m_data.pattern.kind == Repeat_pattern_kind::segments) {
        ImGui::Text("%zu pieces over %.3f m", m_run.count, m_run.space);
        tooltip("One piece per segment of the curve, and the chord lengths they span (Upright: measured in plan).");
    } else if (m_data.pattern.count_mode == Kit_layout_count_mode::gap) {
        ImGui::Text("%zu pieces, actual gap %.3f m over %.3f m", m_run.count, (m_run.count > 1) ? (m_run.step - m_item_extent) : 0.0f, m_run.space);
        tooltip("The count the space holds at the stated gap, the gap once the remainder is spread, and the space (a path: its curve's length).");
    } else {
        ImGui::Text("%zu pieces, step %.3f m over %.3f m", m_run.count, m_run.step, m_run.space);
        tooltip("The count, the step between centers and the space they span (a path: its curve's length).");
    }
}

void Kit_layout_editor::item_shape_line(const std::size_t item)
{
    if (item >= m_items.size()) {
        return;
    }
    const Kit_layout_item_rows& rows = m_items[item];
    if (!rows.problem.empty()) {
        kit_message_line(rows.problem);
        return;
    }
    const Kit_shape_definition* const shape = rows.values.get_shape();
    ImGui::TextUnformatted((shape != nullptr) ? shape->display_name.c_str() : m_data.items[item].shape.c_str());
    if ((shape != nullptr) && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\n%s", shape->id.c_str(), shape->description.c_str());
    }
    kit_stairs_line(rows.values);
    kit_message_line(rows.message);
}

void Kit_layout_editor::item_parameter_row(const std::size_t item, const std::size_t parameter)
{
    if ((item >= m_items.size()) || !m_items[item].problem.empty()) {
        return;
    }
    Kit_layout_item_rows& rows = m_items[item];
    const Kit_row_edit edit = kit_parameter_row(rows.values, parameter, Kit_row_label::recorded, m_lengths);
    if ((edit == Kit_row_edit::committed) || (edit == Kit_row_edit::changed)) {
        rows.message = rows.values.validate();
        if (rows.message.empty()) {
            Kit_layout_data data = m_data;
            data.items[item].parameters = rows.values.to_canonical_text();
            on_row_edit(edit, data);
        } else if (edit == Kit_row_edit::committed) {
            // Refused values: the children before the drag come back and nothing is recorded (D3.6).
            on_row_edit(Kit_row_edit::cancelled, m_data);
        }
    } else {
        on_row_edit(edit, m_data);
    }
}

void Kit_layout_editor::item_material_row(const std::size_t item, const std::size_t slot)
{
    if ((item >= m_items.size()) || !m_items[item].problem.empty()) {
        return;
    }
    const Kit_layout_item_rows&       rows  = m_items[item];
    const Kit_shape_definition* const shape = rows.values.get_shape();
    if ((shape == nullptr) || (slot >= shape->material_slots.size())) {
        return;
    }
    const Kit_slot_palette* const palette = m_context.level_kits->get_library().find_slot_palette(shape->material_slots[slot]);
    const std::string&            current = rows.materials[slot];
    const bool open = ImGui::BeginCombo("##", current.c_str());
    std::optional<std::string> chosen;
    if (open) {
        const auto option = [&](const std::string& name) {
            if (ImGui::Selectable(name.c_str(), name == current) && (name != current)) {
                chosen = name;
            }
        };
        if (palette != nullptr) {
            option(palette->default_material);
            for (const std::string& alternative : palette->alternatives) {
                option(alternative);
            }
        }
        ImGui::EndCombo();
    }
    tooltip("The palette material of this slot for the item's pieces: its default first, then its alternatives.");
    if (chosen.has_value()) {
        Kit_layout_data data = m_data;
        data.items[item].materials.resize(shape->material_slots.size());
        data.items[item].materials[slot] = chosen.value();
        commit(data);
    }
}

void Kit_layout_editor::item_weight_row(const std::size_t item)
{
    if (item >= m_data.items.size()) {
        return;
    }
    // The row edits the shown copy in place, as the pattern rows do.
    int weight = static_cast<int>(m_data.items[item].weight);
    const bool changed = ImGui::DragInt("##", &weight, 0.1f, 0, static_cast<int>(c_max_scatter_weight), "%d", ImGuiSliderFlags_AlwaysClamp);
    const Kit_row_edit edit = drag_edit(changed);
    tooltip("The weight a scatter picks this item by for each point, against the other items' weights; 0 never picks it. A line, grid, ring or path takes item i modulo the item count.");
    m_data.items[item].weight = static_cast<uint32_t>(weight);
    on_row_edit(edit, m_data);
}

void Kit_layout_editor::item_remove_button(const std::size_t item)
{
    ImGui::BeginDisabled(m_items.size() < 2);
    if (ImGui::Button("Remove Item") && (item < m_data.items.size())) {
        Kit_layout_data data = m_data;
        data.items.erase(data.items.begin() + static_cast<std::ptrdiff_t>(item));
        commit(data);
    }
    ImGui::EndDisabled();
    tooltip("Removes this item from the layout; a layout keeps at least one item.");
}

void Kit_layout_editor::add_item_button()
{
    const Brush_tool* const             brush_tool = m_context.brush_tool;
    const Kit_shape_definition* const   shape      = (brush_tool != nullptr) ? brush_tool->get_kit_shape() : nullptr;
    ImGui::BeginDisabled(shape == nullptr);
    if (ImGui::Button("Add Shape Item") && (shape != nullptr)) {
        Kit_layout_data data = m_data;
        Kit_layout_item item;
        item.shape      = shape->id;
        item.parameters = brush_tool->get_kit_values().to_canonical_text();
        const std::vector<std::string>& materials = brush_tool->get_kit_slot_materials();
        item.materials.assign(materials.begin(), materials.end());
        data.items.push_back(std::move(item));
        commit(data);
    }
    ImGui::EndDisabled();
    tooltip(
        "Adds the shape chosen in the Level Kit window, with its parameter rows' values and slot materials, as the layout's next item of weight 1; "
        "element i takes item i modulo the item count, a scatter picks each point's item by the weights."
    );
}

void Kit_layout_editor::setting_row(const Kit_layout_setting_row row)
{
    const Kit_row_edit edit = layout_setting_row(m_data, row, Kit_row_label::recorded);
    on_row_edit(edit, m_data);
}

void Kit_layout_editor::lock_buttons()
{
    const bool locked = (m_data.lock == Kit_layout_lock::locked);
    ImGui::BeginDisabled(locked);
    if (ImGui::Button("Lock")) {
        queue_lock(Kit_layout_lock::locked);
    }
    name_role(Kit_row_label::recorded, "lock");
    ImGui::EndDisabled();
    tooltip("Locks the layout: every child is replaced by the pieces its values make, which DISCARDS the edits made to the children while unlocked. One undo step.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!locked);
    if (ImGui::Button("Unlock")) {
        queue_lock(Kit_layout_lock::unlocked);
    }
    name_role(Kit_row_label::recorded, "unlock");
    tooltip("Unlocks the layout: no child changes, and the children are then edited directly. One undo step.");
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        queue_refresh();
    }
    name_role(Kit_row_label::recorded, "refresh");
    tooltip("Makes the children equal to the values again, keeping the ones that match (a new module, a changed surface below). One undo step when anything changes.");
    ImGui::EndDisabled();
}

void Kit_layout_editor::message_line() const
{
    if (m_failed) {
        kit_message_line(m_message);
    } else if (!m_message.empty()) {
        ImGui::TextWrapped("%s", m_message.c_str());
    }
}

void Kit_layout_editor::read_layout(Scene_root& scene_root, const erhe::scene::Node& layout)
{
    Level_kits& level_kits = *m_context.level_kits;
    m_ready   = true;
    m_dirty   = false;
    m_kinds   = get_kinds(layout);
    m_problem = read_kit_layout(layout, m_data);
    m_items.clear();
    m_run = Kit_layout_run{};
    m_run_problem.clear();
    m_item_extent = 0.0f;
    m_item_sizes.clear();
    if (!m_problem.empty()) {
        return;
    }
    m_lengths = level_kits.get_lengths(scene_root);
    const Kit_shape_library& library = level_kits.get_library();
    for (const Kit_layout_item& item : m_data.items) {
        Kit_layout_item_rows& rows = m_items.emplace_back();
        rows.label = fmt::format("Item {}", m_items.size());
        Kit_placement placement;
        rows.problem = level_kits.read_layout_item(scene_root, item, placement);
        if (!rows.problem.empty()) {
            m_item_sizes.add(item, erhe::math::Aabb{});
            continue;
        }
        rows.values = placement.values.value();
        rows.label  = fmt::format("Item {}: {}", m_items.size(), placement.shape->display_name);
        for (std::size_t slot = 0, end = placement.shape->material_slots.size(); slot < end; ++slot) {
            const Kit_palette_material* const fallback = library.find_slot_default(placement.shape->material_slots[slot]);
            const bool given = (slot < item.materials.size()) && !item.materials[slot].empty();
            rows.materials.push_back(given ? item.materials[slot] : ((fallback != nullptr) ? fallback->name : std::string{}));
        }
        m_item_sizes.add(item, level_kits.get_layout_item_bounds(scene_root, item));
    }
    m_item_extent = m_item_sizes.extent;
    m_run_problem = level_kits.resolve_layout_run(layout, m_data, m_item_sizes, m_run);
    read_stairs(layout, m_data);
}

void Kit_layout_editor::read_stairs(const erhe::scene::Node& layout, const Kit_layout_data& data)
{
    m_stairs.clear();
    const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(&layout);
    if ((data.pattern.kind == Repeat_pattern_kind::stairs) && (curve != nullptr)) {
        // A refusal is the run problem; the report then stays empty.
        static_cast<void>(make_stairs(curve->get_table(), data.pattern.stairs, m_stairs, nullptr));
    }
}

void Kit_layout_editor::queue_read()
{
    m_read_queued = true;
    // Prepare phase: an item's brush may be made here and upload its mesh.
    m_context.imgui_windows->queue(
        [this, weak_scene_root = m_scene_root, weak_layout = m_layout]() {
            m_read_queued = false;
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Node> node   = weak_layout.lock();
            if (!target || !node || !m_context.app_scenes->is_host_registered(target.get()) || (node != m_layout.lock())) {
                return;
            }
            read_layout(*target, *node);
        }
    );
}

void Kit_layout_editor::end_lost_edit()
{
    // The immediate-mode change site: ImGui drops the active id of a widget that stops being drawn and reports no end.
    if ((m_session_id != 0) && (ImGui::GetActiveID() != m_session_id)) {
        end_edit_session();
    }
}

void Kit_layout_editor::abandon_edit()
{
    if (m_session_id != 0) {
        end_edit_session();
    }
}

void Kit_layout_editor::end_edit_session()
{
    // As a cancel: a locked layout's preview gives the children and values back, an unlocked one's pending outline goes.
    m_session_id = 0;
    queue_end_preview();
    m_context.imgui_windows->queue(
        [this]() {
            m_context.level_kits->get_layout_indicators().end_pending();
        }
    );
    m_dirty = true;
}

void Kit_layout_editor::on_row_edit(const Kit_row_edit edit, const Kit_layout_data& data)
{
    if ((edit == Kit_row_edit::committed) || (edit == Kit_row_edit::cancelled)) {
        m_session_id = 0;
    } else if ((edit == Kit_row_edit::changed) && (m_session_id == 0)) {
        m_session_id = ImGui::GetActiveID();
    }
    switch (edit) {
        case Kit_row_edit::changed: {
            // Unlocked, the values change alone at the commit; until then the outline shows them.
            if (data.lock == Kit_layout_lock::locked) {
                queue_preview(data);
            } else {
                queue_pending_outline(data);
            }
            break;
        }
        case Kit_row_edit::committed: {
            commit(data);
            break;
        }
        case Kit_row_edit::cancelled: {
            end_edit_session();
            break;
        }
        default: {
            break;
        }
    }
}

void Kit_layout_editor::commit(const Kit_layout_data& data)
{
    queue_set(data);
}

void Kit_layout_editor::queue_preview(const Kit_layout_data& data)
{
    // Prepare phase, as the commit: a brush of the new values uploads its mesh.
    m_context.imgui_windows->queue(
        [this, weak_scene_root = m_scene_root, weak_layout = m_layout, data]() {
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Node> node   = weak_layout.lock();
            // A node deselected mid-drag has ended its preview; a later step does not begin another.
            if (!target || !node || !m_context.app_scenes->is_host_registered(target.get()) || !node->is_selected()) {
                return;
            }
            Kit_layout_result result;
            const std::string error = m_context.level_kits->preview_layout_values(node, data, result);
            m_failed  = !error.empty();
            m_message = error;
            if (!m_failed) {
                m_run         = result.run;
                m_item_extent = result.item_extent;
                m_run_problem.clear();
                m_stairs = result.stairs;
            }
        }
    );
}

void Kit_layout_editor::queue_pending_outline(const Kit_layout_data& data)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = m_scene_root, weak_layout = m_layout, data]() {
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Node> node   = weak_layout.lock();
            if (!target || !node || !m_context.app_scenes->is_host_registered(target.get()) || !node->is_selected()) {
                return;
            }
            m_context.level_kits->get_layout_indicators().set_pending_values(*target, *node, data);
        }
    );
}

void Kit_layout_editor::queue_end_preview()
{
    m_context.imgui_windows->queue(
        [this, weak_layout = m_layout]() {
            const std::shared_ptr<erhe::scene::Node> node = weak_layout.lock();
            if (node && m_context.level_kits->is_layout_previewing(*node)) {
                m_context.level_kits->end_layout_preview();
            }
        }
    );
}

void Kit_layout_editor::queue_set(const Kit_layout_data& data)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = m_scene_root, weak_layout = m_layout, data]() {
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Node> node   = weak_layout.lock();
            if (!target || !node || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            // The committed values are drawn from the node again.
            m_context.level_kits->get_layout_indicators().end_pending();
            const Kit_layout_result result = m_context.level_kits->set_layout(*target, node, data);
            m_failed = !result.error.empty();
            if (m_failed) {
                log_scene->warn("Kit Layout: '{}' edit refused: {}", node->get_name(), result.error);
            }
            m_message = m_failed
                ? result.error
                : (result.changed ? fmt::format("Set the values of '{}': {} pieces kept, {} added, {} removed.", node->get_name(), result.kept_count, result.added_count, result.removed_count) : std::string{});
            // A refused or unchanged edit shows the node's values again.
            m_dirty = true;
        }
    );
}

void Kit_layout_editor::queue_lock(const Kit_layout_lock lock)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = m_scene_root, weak_layout = m_layout, lock]() {
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Node> node   = weak_layout.lock();
            if (!target || !node || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            const Kit_layout_result result = m_context.level_kits->set_layout_lock(*target, node, lock);
            m_failed = !result.error.empty();
            if (m_failed) {
                log_scene->warn("Kit Layout: '{}' lock refused: {}", node->get_name(), result.error);
                m_message = result.error;
            } else if (lock == Kit_layout_lock::locked) {
                m_message = fmt::format("Locked '{}': {} pieces made.", node->get_name(), result.added_count);
            } else {
                m_message = fmt::format("Unlocked '{}'.", node->get_name());
            }
            m_dirty = true;
        }
    );
}

void Kit_layout_editor::queue_refresh()
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = m_scene_root, weak_layout = m_layout]() {
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Node> node   = weak_layout.lock();
            if (!target || !node || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            const Kit_layout_result result = m_context.level_kits->refresh_layout(*target, node);
            m_failed = !result.error.empty();
            if (m_failed) {
                log_scene->warn("Kit Layout: '{}' refresh refused: {}", node->get_name(), result.error);
            }
            m_message = m_failed
                ? result.error
                : (result.changed
                    ? fmt::format("Refreshed '{}': {} pieces kept, {} added, {} removed.", node->get_name(), result.kept_count, result.added_count, result.removed_count)
                    : fmt::format("'{}' already matches its values.", node->get_name()));
            m_dirty = true;
        }
    );
}

Kit_layout_initial_editor::Kit_layout_initial_editor(App_context& context)
    : m_context{context}
{
}

void Kit_layout_initial_editor::store()
{
    // A zero or non-finite direction is refused as the Properties rows and the MCP tools refuse it; the stored one stays.
    const std::string direction_error = check_surface_direction(m_values.data.surface_direction);
    if (!direction_error.empty()) {
        log_scene->warn("Level Kit window: {}; the initial Surface Direction is kept", direction_error);
        m_values.data.surface_direction = m_context.level_kits->get_layout_initial_values().data.surface_direction;
    }
    m_context.level_kits->set_layout_initial_values(m_values);
}

void Kit_layout_initial_editor::imgui(const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Mesh>& piece)
{
    if (!m_loaded) {
        m_values = m_context.level_kits->get_layout_initial_values();
        m_loaded = true;
    }
    ImGui::PushID("layout");
    ImGui::TextWrapped("The values a new kit layout node starts with; a selected layout node's values are edited in the Properties window.");
    bool changed = false;
    for (const Kit_layout_row row : get_layout_rows(m_values.data.pattern)) {
        const bool spaced = (row == Kit_layout_row::step) || (row == Kit_layout_row::spacing_x) || (row == Kit_layout_row::spacing_z);
        ImGui::BeginDisabled(spaced && (m_values.spacing == Kit_layout_spacing::from_item));
        changed = (layout_pattern_row(m_values.data.pattern, row, Kit_layout_kinds::all, Kit_row_label::beside, m_context.level_kits->get_library()) == Kit_row_edit::committed) || changed;
        ImGui::EndDisabled();
    }
    bool from_item = (m_values.spacing == Kit_layout_spacing::from_item);
    if (ImGui::Checkbox("Spacing From Item", &from_item)) {
        m_values.spacing = from_item ? Kit_layout_spacing::from_item : Kit_layout_spacing::as_given;
        changed = true;
    }
    tooltip("The new layout's line step and grid spacings are its item's extent, a contiguous run; off, Step and Spacing are as given.");
    for (const Kit_layout_setting_row row : get_layout_setting_rows(m_values.data.pattern)) {
        changed = (layout_setting_row(m_values.data, row, Kit_row_label::beside) == Kit_row_edit::committed) || changed;
    }
    bool locked = (m_values.data.lock == Kit_layout_lock::locked);
    if (ImGui::Checkbox("Locked", &locked)) {
        m_values.data.lock = locked ? Kit_layout_lock::locked : Kit_layout_lock::unlocked;
        changed = true;
    }
    tooltip("Locked, the new layout makes its children and they are not edited directly.");
    if (changed) {
        store();
    }

    // U47: a new node of the shown kind needs no selection; its own button names the kind ("Add Path Stairs").
    const Repeat_pattern_kind kind = m_values.data.pattern.kind;
    ImGui::BeginDisabled(!scene_root);
    if (ImGui::Button(get_layout_add_button_label(kind))) {
        queue_add(scene_root, kind);
    }
    ImGui::EndDisabled();
    tooltip(
        (kind == Repeat_pattern_kind::stairs)
            ? "Adds a staircase generated along a new curve (a stairs kit layout) at the scene's origin with the values above: a flight "
              "rising along +x. Edit the curve's points with the node selected to shape it. One undo step; selects the node."
            : "Adds a kit layout node of this kind at the scene's origin with the values above and the shape chosen above as its item "
              "(a basic block when none is chosen). One undo step; selects the node."
    );
    const bool stairs = (kind == Repeat_pattern_kind::stairs) || (kind == Repeat_pattern_kind::floor);
    ImGui::BeginDisabled(!piece || stairs);
    if (ImGui::Button("Make Layout")) {
        queue_make(scene_root, piece);
    }
    ImGui::EndDisabled();
    tooltip(
        stairs
            ? "A stairs layout places no piece: use the button above, or Make Layout On Curve with a curve selected."
            : "Makes a kit layout node in the selected piece's place with the piece's shape, values and slot materials as its item and the values above; "
              "the piece becomes its element 0 (a path or segments layout is a curve node, straight along the piece's x axis). One undo step; selects the node."
    );
    if (piece) {
        ImGui::SameLine();
        ImGui::Text("from %s", piece->get_name().c_str());
    }
    const std::shared_ptr<erhe::scene::Curve> curve = scene_root ? find_plain_curve(*scene_root) : std::shared_ptr<erhe::scene::Curve>{};
    ImGui::BeginDisabled(!curve || !is_curve_layout_kind(kind));
    if (ImGui::Button("Make Layout On Curve")) {
        queue_make_on_curve(scene_root, curve, kind);
    }
    ImGui::EndDisabled();
    tooltip(
        is_curve_layout_kind(kind)
            ? "Makes the selected curve node (no children, not a layout yet) a kit layout of this kind with the values above, "
              "laid out along the curve as it is. One undo step."
            : "Only a path, segments or stairs layout is a curve node: choose one of these kinds."
    );
    if (curve) {
        ImGui::SameLine();
        ImGui::Text("on %s", curve->get_name().c_str());
    }
    bool as_layout = (m_values.place_as == Kit_place_as::layout);
    if (ImGui::Checkbox("Place As Layout", &as_layout)) {
        m_values.place_as = as_layout ? Kit_place_as::layout : Kit_place_as::piece;
        store();
    }
    tooltip("A Brush tool click in kit mode places a kit layout node of the chosen shape with the values above, element 0 at the preview, and selects it, instead of one piece.");
    if (m_failed) {
        kit_message_line(m_message);
    } else if (!m_message.empty()) {
        ImGui::TextWrapped("%s", m_message.c_str());
    }
    ImGui::PopID();
}

auto Kit_layout_initial_editor::get_layout_add_button_label(const Repeat_pattern_kind kind) -> const char*
{
    switch (kind) {
        case Repeat_pattern_kind::line:     return "Add Line Layout";
        case Repeat_pattern_kind::grid:     return "Add Grid Layout";
        case Repeat_pattern_kind::ring:     return "Add Ring Layout";
        case Repeat_pattern_kind::path:     return "Add Path Layout";
        case Repeat_pattern_kind::scatter:  return "Add Scatter Layout";
        case Repeat_pattern_kind::segments: return "Add Segments Layout";
        case Repeat_pattern_kind::stairs:   return "Add Path Stairs";
        case Repeat_pattern_kind::floor:    return "Add Polygon Floor";
        default:                            return "Add Layout";
    }
}

auto Kit_layout_initial_editor::find_plain_curve(const Scene_root& scene_root) const -> std::shared_ptr<erhe::scene::Curve>
{
    if (m_context.selection == nullptr) {
        return {};
    }
    for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
        if (!item || (item->get_item_host() != &scene_root)) {
            continue;
        }
        const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(item.get());
        if ((curve != nullptr) && !carries_kit_layout(*curve) && (curve->get_child_count() == 0)) {
            return std::static_pointer_cast<erhe::scene::Curve>(item);
        }
    }
    return {};
}

auto Kit_layout_initial_editor::get_chosen_item() const -> std::optional<Kit_layout_item>
{
    const Brush_tool* const           brush_tool = m_context.brush_tool;
    const Kit_shape_definition* const shape      = (brush_tool != nullptr) ? brush_tool->get_kit_shape() : nullptr;
    if (shape == nullptr) {
        return std::nullopt;
    }
    Kit_layout_item item;
    item.shape      = shape->id;
    item.parameters = brush_tool->get_kit_values().to_canonical_text();
    const std::vector<std::string>& materials = brush_tool->get_kit_slot_materials();
    item.materials.assign(materials.begin(), materials.end());
    return item;
}

void Kit_layout_initial_editor::queue_add(const std::shared_ptr<Scene_root>& scene_root, const Repeat_pattern_kind kind)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, kind, item = get_chosen_item()]() {
            const std::shared_ptr<Scene_root> target = weak_scene_root.lock();
            if (!target || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            const Kit_layout_result result = m_context.level_kits->add_layout_of_kind(
                *target, kind, {}, glm::vec3{0.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, item
            );
            m_failed  = !result.error.empty();
            m_message = m_failed ? result.error : fmt::format("Added '{}' with {} pieces.", result.node->get_name(), result.added_count);
            if (m_failed) {
                log_scene->warn("Level Kit: {} refused: {}", get_layout_kind_create_label(kind), result.error);
            }
        }
    );
}

void Kit_layout_initial_editor::queue_make_on_curve(const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Curve>& curve, const Repeat_pattern_kind kind)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, weak_curve = std::weak_ptr<erhe::scene::Curve>{curve}, kind, item = get_chosen_item()]() {
            const std::shared_ptr<Scene_root>         target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Curve> node   = weak_curve.lock();
            if (!target || !node || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            const Kit_layout_result result = m_context.level_kits->make_initial_layout_on_curve(*target, node, kind, item);
            m_failed  = !result.error.empty();
            m_message = m_failed ? result.error : fmt::format("Made '{}' a {} with {} pieces.", node->get_name(), get_layout_kind_create_label(kind), result.added_count);
            if (m_failed) {
                log_scene->warn("Level Kit: layout on curve '{}' refused: {}", node->get_name(), result.error);
            }
        }
    );
}

void Kit_layout_initial_editor::queue_make(const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Mesh>& piece)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, weak_piece = std::weak_ptr<erhe::scene::Mesh>{piece}]() {
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Mesh> mesh   = weak_piece.lock();
            if (!target || !mesh || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            const Kit_layout_result result = m_context.level_kits->make_layout_from_piece_initial(*target, mesh);
            m_failed  = !result.error.empty();
            m_message = m_failed ? result.error : fmt::format("Made '{}' with {} pieces.", result.node->get_name(), result.added_count);
            if (m_failed) {
                log_scene->warn("Level Kit: layout from '{}' refused: {}", mesh->get_name(), result.error);
            } else {
                m_context.selection->set_selection({result.node}, result.node);
            }
        }
    );
}

} // namespace editor
