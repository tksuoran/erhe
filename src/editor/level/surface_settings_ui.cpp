#include "level/surface_settings_ui.hpp"

#include "level/level_kits.hpp"
#include "level/level_queries.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_item_recorder.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

// From windows/IconsMaterialDesignIcons.h, as scene_view.cpp takes its icons.
#define ICON_MDI_ARROW_COLLAPSE_DOWN "\xf3\xb0\x9e\x92" // U+F0792
#define ICON_MDI_SLOPE_UPHILL        "\xf3\xb0\xb8\x80" // U+F0E00
#define ICON_MDI_MAGNET              "\xf3\xb0\x8d\x87" // U+F0347
#define ICON_MDI_AXIS_ARROW_LOCK     "\xf3\xb0\xb5\x8a" // U+F0D4A
#define ICON_MDI_ROTATE_3D_VARIANT   "\xf3\xb0\x91\xa4" // U+F0464
#define ICON_MDI_AXIS_Y_ROTATE_COUNTERCLOCKWISE "\xf3\xb0\xb5\x94" // U+F0D54
#define ICON_MDI_CAMERA_OUTLINE      "\xf3\xb0\xb5\x9d" // U+F0D5D
#define ICON_MDI_ANCHOR              "\xf3\xb0\x80\xb1" // U+F0031

namespace editor {

namespace {

constexpr const char* c_snap_label     = "Snap To Surface";
constexpr const char* c_orient_label   = "Orient To Surface";
constexpr const char* c_snap_tooltip   = "Snap To Surface: kit placement snaps a placed piece's base to the surface; a Transform tool translate drag snaps the transform anchor (the node origin; in BBox mode the bounding box center) to the nearest surface along the drag direction.";
constexpr const char* c_connector_label   = "Connector Snap";
constexpr const char* c_connector_tooltip = "Connector Snap (m): a Transform tool drag (translate, rotate or scale) moves the dragged nodes so that their connector nearest another kit piece's connector meets it when within this distance; 0 = off.";
constexpr const char* c_connector_axis_label   = "Connector Snap On Axis";
constexpr const char* c_connector_axis_tooltip = "Connector Snap On Axis: an axis or plane drag applies only the snap offset's part along the dragged axis or in the dragged plane, so the piece stays on it and the pair may stay apart; off, the pair meets.";
constexpr const char* c_connector_orientation_label   = "Connector Snap Orientation";
constexpr const char* c_connector_orientation_tooltip = "Connector Snap Orientation: Off - a Transform tool drag's connector snap translates only. Full - it also turns the moved nodes about the moved connector so the two connector frames face each other, as a connector join does, overriding Orient To Surface. Rotate - with Orient To Surface on it turns them only about their up axis (the surface normal) as far as that turn faces the pair; without it as Full. Every drag kind: translate, axis and plane handles, rotate and scale.";
constexpr const char* c_max_distance_label   = "Surface Max Distance";
constexpr const char* c_max_distance_tooltip = "Surface Max Distance (m): Snap To Surface moves each mesh at most this far along the snap direction, up or down, to the nearest place where it rests and fits; beyond, it stays.";
constexpr const char* c_rest_label          = "Surface Rest";
constexpr const char* c_rest_tooltip        = "Surface Rest: with Orient To Surface off, Transform tool drags and Brush tool kit placement rest the bounds face leading along the search direction flush on the surface (Leading Face), or land the anchor point - the gizmo anchor, the placed piece's origin - on it (Anchor). Orient To Surface rests the -Y face; kit layouts always rest the leading face.";
constexpr const char* c_rest_anchor_label   = "Surface Rest Anchor";
constexpr const char* c_rest_anchor_tooltip = "Surface Rest Anchor: on, snap without orient lands the anchor point (the gizmo anchor, the placed piece's origin) on the surface; off, the bounds face leading along the search direction rests flush.";
constexpr const char* c_drag_mode_label     = "Surface Drag Direction Mode";
constexpr const char* c_drag_mode_tooltip   = "Surface Drag Direction Mode: a Transform tool drag snaps the transform anchor (the node origin; in BBox mode the bounding box center) along the explicit Surface Drag Direction, or along the ray from the viewport camera through the anchor.";
constexpr const char* c_drag_vector_label   = "Surface Drag Direction";
constexpr const char* c_drag_vector_tooltip = "Surface Drag Direction: the world direction a Transform tool drag snaps the transform anchor along with the explicit mode; stored as edited and normalized on use, a zero vector is refused.";
constexpr const char* c_camera_ray_label    = "Surface Camera Ray";
constexpr const char* c_camera_ray_tooltip  = "Surface Camera Ray: a Transform tool drag snaps the transform anchor along the ray from the viewport camera through it; off, along the Surface Drag Direction.";
constexpr const char* c_orient_tooltip = "Orient To Surface: the moved mesh's up axis takes the surface normal, keeping its heading.";

void tooltip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text);
    }
}

[[nodiscard]] auto checkbox(const char* label, const char* hidden_label, const Surface_widget_label label_mode, const bool value) -> bool
{
    bool edited_value = value;
    const bool changed = ImGui::Checkbox((label_mode == Surface_widget_label::shown) ? label : hidden_label, &edited_value);
    return changed && (edited_value != value);
}

// A toggle button reports its state to the item recorder the way a checkbox does.
[[nodiscard]] auto toggle_button(
    ImFont* const    icon_font,
    const float      font_size,
    const char*      icon_label,
    const char*      text_label,
    const char*      name,
    const bool       value
) -> bool
{
    const erhe::imgui::Item_mode mode = value ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal;
    erhe::imgui::begin_button_style(mode);
    bool pressed = false;
    if (icon_font != nullptr) {
        ImGui::PushFont(icon_font, font_size);
        pressed = ImGui::Button(icon_label);
        ImGui::PopFont();
    } else {
        pressed = ImGui::Button(text_label);
    }
    erhe::imgui::end_button_style(mode);
    ImGuiContext& g = *ImGui::GetCurrentContext();
    IMGUI_TEST_ENGINE_ITEM_INFO(
        g.LastItemData.ID, name, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable | (value ? ImGuiItemStatusFlags_Checked : 0)
    );
    erhe::imgui::set_item_debug_label(name);
    return pressed;
}

} // anonymous namespace

void surface_snap_checkbox(Level_kits& level_kits, const Surface_widget_label label)
{
    const bool value = (level_kits.get_surface_settings().snap == Surface_snap::on);
    if (checkbox(c_snap_label, "##snap_to_surface", label, value)) {
        level_kits.set_surface_snap(value ? Surface_snap::off : Surface_snap::on);
    }
    tooltip(c_snap_tooltip);
}

void surface_orient_checkbox(Level_kits& level_kits, const Surface_widget_label label)
{
    const bool value = (level_kits.get_surface_settings().orient == Surface_orient::on);
    if (checkbox(c_orient_label, "##orient_to_surface", label, value)) {
        level_kits.set_surface_orient(value ? Surface_orient::off : Surface_orient::on);
    }
    tooltip(c_orient_tooltip);
}

void surface_max_distance_input(Level_kits& level_kits, const Surface_widget_label label)
{
    float value = level_kits.get_surface_settings().forward_distance; // the settings search the distance either way
    const bool changed = ImGui::DragFloat(
        (label == Surface_widget_label::shown) ? "Surface Max Distance##surface_max_distance" : "##surface_max_distance",
        &value, 0.01f, 0.0f, 1000.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp
    );
    erhe::imgui::set_item_debug_label(c_max_distance_label);
    if (changed) {
        level_kits.set_surface_max_distance(value);
    }
    tooltip(c_max_distance_tooltip);
}

void surface_rest_combo(Level_kits& level_kits, const Surface_widget_label label)
{
    const char* const items[] = {"Leading Face", "Anchor"};
    int index = (level_kits.get_surface_rest() == Surface_snap_rest::anchor) ? 1 : 0;
    const bool changed = ImGui::Combo(
        (label == Surface_widget_label::shown) ? "Surface Rest##surface_rest" : "##surface_rest",
        &index, items, IM_ARRAYSIZE(items)
    );
    erhe::imgui::set_item_debug_label(c_rest_label);
    if (changed) {
        level_kits.set_surface_rest((index == 1) ? Surface_snap_rest::anchor : Surface_snap_rest::leading_face);
    }
    tooltip(c_rest_tooltip);
}

void surface_drag_direction_mode_combo(Level_kits& level_kits, const Surface_widget_label label)
{
    const Surface_drag_direction mode = level_kits.get_surface_drag_direction_mode();
    const char* const items[] = {"Explicit", "Camera Ray"};
    int index = (mode == Surface_drag_direction::camera_ray) ? 1 : 0;
    const bool changed = ImGui::Combo(
        (label == Surface_widget_label::shown) ? "Surface Drag Direction Mode##surface_drag_direction_mode" : "##surface_drag_direction_mode",
        &index, items, IM_ARRAYSIZE(items)
    );
    erhe::imgui::set_item_debug_label(c_drag_mode_label);
    if (changed) {
        level_kits.set_surface_drag_direction_mode((index == 1) ? Surface_drag_direction::camera_ray : Surface_drag_direction::explicit_vector);
    }
    tooltip(c_drag_mode_tooltip);
}

void surface_drag_direction_input(Level_kits& level_kits, const Surface_widget_label label)
{
    glm::vec3 value = level_kits.get_surface_drag_direction();
    const bool changed = ImGui::DragFloat3(
        (label == Surface_widget_label::shown) ? "Surface Drag Direction##surface_drag_direction" : "##surface_drag_direction",
        &value.x, 0.01f, -1.0f, 1.0f, "%.2f"
    );
    erhe::imgui::set_item_debug_label(c_drag_vector_label);
    if (changed && check_surface_direction(value).empty()) {
        level_kits.set_surface_drag_direction(value);
    }
    tooltip(c_drag_vector_tooltip);
}

void connector_snap_distance_input(Level_kits& level_kits, const Surface_widget_label label)
{
    float value = level_kits.get_connector_snap_distance();
    const bool changed = ImGui::DragFloat(
        (label == Surface_widget_label::shown) ? "Connector Snap##connector_snap_distance" : "##connector_snap_distance",
        &value, 0.01f, 0.0f, 100.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp
    );
    erhe::imgui::set_item_debug_label(c_connector_label);
    if (changed) {
        level_kits.set_connector_snap_distance(value);
    }
    tooltip(c_connector_tooltip);
}

void connector_snap_axis_checkbox(Level_kits& level_kits, const Surface_widget_label label)
{
    const bool value = (level_kits.get_connector_snap_axis_mode() == Connector_snap_axis_mode::stay_on_axis);
    if (checkbox(c_connector_axis_label, "##connector_snap_axis_mode", label, value)) {
        level_kits.set_connector_snap_axis_mode(value ? Connector_snap_axis_mode::full_offset : Connector_snap_axis_mode::stay_on_axis);
    }
    erhe::imgui::set_item_debug_label(c_connector_axis_label);
    tooltip(c_connector_axis_tooltip);
}

void connector_snap_orientation_combo(Level_kits& level_kits, const Surface_widget_label label)
{
    // The item names carry the widget's id, so a click on one in the open popup names it uniquely.
    const char* const items[] = {"Off##connector_snap_orientation", "Rotate##connector_snap_orientation", "Full##connector_snap_orientation"};
    const Connector_snap_orientation orientation = level_kits.get_connector_snap_orientation();
    int index =
        (orientation == Connector_snap_orientation::rotate) ? 1 :
        (orientation == Connector_snap_orientation::full)   ? 2 :
                                                              0;
    const bool changed = ImGui::Combo(
        (label == Surface_widget_label::shown) ? "Connector Snap Orientation##connector_snap_orientation" : "##connector_snap_orientation",
        &index, items, IM_ARRAYSIZE(items)
    );
    erhe::imgui::set_item_debug_label(c_connector_orientation_label);
    if (changed) {
        level_kits.set_connector_snap_orientation(
            (index == 1) ? Connector_snap_orientation::rotate :
            (index == 2) ? Connector_snap_orientation::full   :
                           Connector_snap_orientation::off
        );
    }
    tooltip(c_connector_orientation_tooltip);
}

void surface_settings_checkboxes(Level_kits& level_kits)
{
    surface_snap_checkbox        (level_kits, Surface_widget_label::shown);
    surface_rest_combo           (level_kits, Surface_widget_label::shown);
    surface_max_distance_input   (level_kits, Surface_widget_label::shown);
    surface_orient_checkbox      (level_kits, Surface_widget_label::shown);
    surface_drag_direction_mode_combo(level_kits, Surface_widget_label::shown);
    surface_drag_direction_input (level_kits, Surface_widget_label::shown);
    connector_snap_distance_input(level_kits, Surface_widget_label::shown);
    connector_snap_axis_checkbox (level_kits, Surface_widget_label::shown);
    connector_snap_orientation_combo(level_kits, Surface_widget_label::shown);
}

void surface_settings_toolbar_buttons(Level_kits& level_kits, ImFont* const icon_font, const float font_size)
{
    ImGui::PushID("surface_settings_toolbar_buttons");
    const Surface_options options = level_kits.get_surface_settings();
    const bool snap   = (options.snap   == Surface_snap::on);
    const bool orient = (options.orient == Surface_orient::on);
    if (toggle_button(icon_font, font_size, ICON_MDI_ARROW_COLLAPSE_DOWN "##snap_to_surface", "Snap##snap_to_surface", c_snap_label, snap)) {
        level_kits.set_surface_snap(snap ? Surface_snap::off : Surface_snap::on);
    }
    tooltip(c_snap_tooltip);
    ImGui::SameLine(0.0f, 2.0f);
    const bool rest_anchor = (level_kits.get_surface_rest() == Surface_snap_rest::anchor);
    if (toggle_button(icon_font, font_size, ICON_MDI_ANCHOR "##surface_rest", "Anchor##surface_rest", c_rest_anchor_label, rest_anchor)) {
        level_kits.set_surface_rest(rest_anchor ? Surface_snap_rest::leading_face : Surface_snap_rest::anchor);
    }
    tooltip(c_rest_anchor_tooltip);
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::SetNextItemWidth(4.0f * font_size);
    surface_max_distance_input(level_kits, Surface_widget_label::hidden);
    ImGui::SameLine();
    if (toggle_button(icon_font, font_size, ICON_MDI_SLOPE_UPHILL "##orient_to_surface", "Orient##orient_to_surface", c_orient_label, orient)) {
        level_kits.set_surface_orient(orient ? Surface_orient::off : Surface_orient::on);
    }
    tooltip(c_orient_tooltip);
    ImGui::SameLine(0.0f, 2.0f);
    const bool camera_ray = (level_kits.get_surface_drag_direction_mode() == Surface_drag_direction::camera_ray);
    if (toggle_button(icon_font, font_size, ICON_MDI_CAMERA_OUTLINE "##surface_camera_ray", "Ray##surface_camera_ray", c_camera_ray_label, camera_ray)) {
        level_kits.set_surface_drag_direction_mode(camera_ray ? Surface_drag_direction::explicit_vector : Surface_drag_direction::camera_ray);
    }
    tooltip(c_camera_ray_tooltip);
    ImGui::SameLine();
    // The drag's label in the toolbar's style: the magnet icon, or short text without the icon font.
    ImGui::AlignTextToFramePadding();
    if (icon_font != nullptr) {
        ImGui::PushFont(icon_font, font_size);
        ImGui::TextUnformatted(ICON_MDI_MAGNET);
        ImGui::PopFont();
    } else {
        ImGui::TextUnformatted("Connect");
    }
    tooltip(c_connector_tooltip);
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::SetNextItemWidth(4.0f * font_size);
    connector_snap_distance_input(level_kits, Surface_widget_label::hidden);
    ImGui::SameLine(0.0f, 2.0f);
    const bool on_axis = (level_kits.get_connector_snap_axis_mode() == Connector_snap_axis_mode::stay_on_axis);
    if (toggle_button(icon_font, font_size, ICON_MDI_AXIS_ARROW_LOCK "##connector_snap_axis_mode", "Axis##connector_snap_axis_mode", c_connector_axis_label, on_axis)) {
        level_kits.set_connector_snap_axis_mode(on_axis ? Connector_snap_axis_mode::full_offset : Connector_snap_axis_mode::stay_on_axis);
    }
    tooltip(c_connector_axis_tooltip);
    ImGui::SameLine(0.0f, 2.0f);
    // U50: one button cycles Off, Rotate, Full; its icon and text name the mode, the ### id stays the same.
    const Connector_snap_orientation orientation = level_kits.get_connector_snap_orientation();
    const bool rotate = (orientation == Connector_snap_orientation::rotate);
    if (
        toggle_button(
            icon_font, font_size,
            rotate ? (ICON_MDI_AXIS_Y_ROTATE_COUNTERCLOCKWISE "###connector_snap_orientation") : (ICON_MDI_ROTATE_3D_VARIANT "###connector_snap_orientation"),
            rotate ? "Rotate###connector_snap_orientation" : (orientation == Connector_snap_orientation::full) ? "Full###connector_snap_orientation" : "Turn###connector_snap_orientation",
            c_connector_orientation_label,
            orientation != Connector_snap_orientation::off
        )
    ) {
        level_kits.set_connector_snap_orientation(
            (orientation == Connector_snap_orientation::off) ? Connector_snap_orientation::rotate :
            rotate                                           ? Connector_snap_orientation::full   :
                                                               Connector_snap_orientation::off
        );
    }
    tooltip(c_connector_orientation_tooltip);
    ImGui::PopID();
}

} // namespace editor
