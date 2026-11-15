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

namespace editor {

namespace {

constexpr const char* c_snap_label     = "Snap To Surface";
constexpr const char* c_orient_label   = "Orient To Surface";
constexpr const char* c_snap_tooltip   = "Snap To Surface: kit placement and the Transform tool's translate drags rest the base of each moved mesh on the surface under it.";
constexpr const char* c_connector_label   = "Connector Snap";
constexpr const char* c_connector_tooltip = "Connector Snap (m): a translate drag moves the dragged nodes so that their connector nearest another kit piece's connector meets it when within this distance; 0 = off.";
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

void surface_settings_checkboxes(Level_kits& level_kits)
{
    surface_snap_checkbox        (level_kits, Surface_widget_label::shown);
    surface_orient_checkbox      (level_kits, Surface_widget_label::shown);
    connector_snap_distance_input(level_kits, Surface_widget_label::shown);
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
    ImGui::SameLine();
    if (toggle_button(icon_font, font_size, ICON_MDI_SLOPE_UPHILL "##orient_to_surface", "Orient##orient_to_surface", c_orient_label, orient)) {
        level_kits.set_surface_orient(orient ? Surface_orient::off : Surface_orient::on);
    }
    tooltip(c_orient_tooltip);
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
    ImGui::PopID();
}

} // namespace editor
