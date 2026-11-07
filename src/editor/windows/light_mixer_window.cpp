#include "windows/light_mixer_window.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "editor_item_bits.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <imgui/imgui.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <mutex>

namespace editor {

namespace {

void set_light_flag(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Light>& light, const uint64_t bit, const bool value)
{
    if (((light->get_flag_bits() & bit) != 0) == value) {
        return;
    }
    light->set_flag_bits(bit, value);
    scene_root.on_light_changed(light);
}

constexpr const char* c_type_letters[] = { "D", "P", "S" };

}

void set_light_muted(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Light>& light, const bool muted)
{
    set_light_flag(scene_root, light, Editor_item_flags::light_muted, muted);
}

void set_light_solo(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Light>& light, const bool solo)
{
    set_light_flag(scene_root, light, Editor_item_flags::light_solo, solo);
}

auto is_light_muted(const erhe::scene::Light& light) -> bool
{
    return (light.get_flag_bits() & Editor_item_flags::light_muted) != 0;
}

auto is_light_solo(const erhe::scene::Light& light) -> bool
{
    return (light.get_flag_bits() & Editor_item_flags::light_solo) != 0;
}

Light_mixer_window::Light_mixer_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context,
    App_message_bus&             app_message_bus
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Light Mixer", "light_mixer"}
    , m_context{app_context}
{
    set_min_size(480.0f, 160.0f);

    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            on_close_scene(static_cast<const erhe::Item_host*>(message.scene_root.get()));
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            on_items_removed(*message.removed.get());
        }
    );
}

void Light_mixer_window::on_close_scene(const erhe::Item_host* const closing_host)
{
    if (m_edit_light && (static_cast<const erhe::Item_host*>(m_edit_light->get_item_host()) == closing_host)) {
        m_edit_light.reset();
        m_edit_property = nullptr;
        m_edit_before.reset();
    }
}

void Light_mixer_window::on_items_removed(const Removed_items& removed)
{
    if (m_edit_light && removed.lookup.contains(m_edit_light.get())) {
        m_edit_light.reset();
        m_edit_property = nullptr;
        m_edit_before.reset();
    }
}

void Light_mixer_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<Scene_root> scene_root = (m_context.selection != nullptr) ? m_context.selection->get_active_scene_root() : std::shared_ptr<Scene_root>{};
    if (!scene_root) {
        end_edit();
        ImGui::TextUnformatted("No active scene");
        return;
    }

    m_lights.clear();
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root->item_host_mutex};
        const erhe::scene::Light_layer* light_layer = scene_root->layers().light();
        if (light_layer != nullptr) {
            for (const std::shared_ptr<erhe::scene::Light>& light : light_layer->lights) {
                if (light) {
                    m_lights.push_back(light);
                }
            }
        }
    }
    if (m_lights.empty()) {
        end_edit();
        ImGui::TextUnformatted("No lights in the active scene");
        return;
    }

    const bool any_solo = std::any_of(
        m_lights.begin(), m_lights.end(),
        [](const std::shared_ptr<erhe::scene::Light>& light) -> bool { return is_light_solo(*light); }
    );

    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##light_mixer", 9, table_flags)) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Light",     ImGuiTableColumnFlags_WidthFixed,   140.0f);
        ImGui::TableSetupColumn("M",         ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("S",         ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Type",      ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Intensity", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Color",     ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Range",     ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Inner",     ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Outer",     ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (const std::shared_ptr<erhe::scene::Light>& light : m_lights) {
            row_imgui(*scene_root, light, any_solo);
        }
        ImGui::EndTable();
    }

    if (any_solo) {
        if (ImGui::Button("Clear Solo")) {
            for (const std::shared_ptr<erhe::scene::Light>& light : m_lights) {
                set_light_solo(*scene_root, light, false);
            }
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Unmute All")) {
        for (const std::shared_ptr<erhe::scene::Light>& light : m_lights) {
            set_light_muted(*scene_root, light, false);
        }
    }

    // No reference outlives the draw (doc/editor/coding_rules.md
    // "Scene-hosted references in editor parts").
    m_lights.clear();
}

void Light_mixer_window::row_imgui(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Light>& light, const bool any_solo)
{
    ImGui::PushID(light.get());
    ImGui::TableNextRow();

    const bool muted = is_light_muted(*light);
    const bool solo  = is_light_solo (*light);
    const bool shaded_by_filter = any_solo ? solo : !muted;

    ImGui::TableNextColumn();
    if (!shaded_by_filter) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    if (ImGui::Selectable(light->get_name().c_str(), light->is_selected())) {
        if (m_context.imgui_windows != nullptr) {
            std::weak_ptr<erhe::scene::Light> weak_light = light;
            m_context.imgui_windows->queue(
                [this, weak_light]() {
                    const std::shared_ptr<erhe::scene::Light> target = weak_light.lock();
                    if (target && (m_context.selection != nullptr)) {
                        m_context.selection->set_selection({target});
                    }
                }
            );
        }
    }
    if (!shaded_by_filter) {
        ImGui::PopStyleColor();
    }

    // Mute column: disabled while any light is solo; its state is kept.
    ImGui::TableNextColumn();
    ImGui::BeginDisabled(any_solo);
    if (erhe::imgui::make_small_button("M", muted ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal)) {
        set_light_muted(scene_root, light, !muted);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(any_solo ? "Mute (inactive while a light is solo)" : "Mute");
    }

    ImGui::TableNextColumn();
    if (erhe::imgui::make_small_button("S", solo ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal)) {
        set_light_solo(scene_root, light, !solo);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Solo");
    }

    const erhe::scene::Light_type type = light->get_light_type();
    ImGui::TableNextColumn();
    const std::size_t type_index = static_cast<std::size_t>(type);
    ImGui::TextUnformatted((type_index < std::size(c_type_letters)) ? c_type_letters[type_index] : "?");
    if (ImGui::IsItemHovered() && (type_index < std::size(erhe::scene::Light::c_type_strings))) {
        ImGui::SetTooltip("%s", erhe::scene::Light::c_type_strings[type_index]);
    }

    ImGui::TableNextColumn();
    float_slider(light, erhe::scene::Light::intensity_property.get(), false);

    ImGui::TableNextColumn();
    color_button(light);

    ImGui::TableNextColumn();
    if (type != erhe::scene::Light_type::directional) {
        float_slider(light, erhe::scene::Light::range_property.get(), false);
    }

    ImGui::TableNextColumn();
    if (type == erhe::scene::Light_type::spot) {
        float_slider(light, erhe::scene::Light::inner_spot_angle_property.get(), true);
    }
    ImGui::TableNextColumn();
    if (type == erhe::scene::Light_type::spot) {
        float_slider(light, erhe::scene::Light::outer_spot_angle_property.get(), true);
    }

    ImGui::PopID();
}

void Light_mixer_window::float_slider(
    const std::shared_ptr<erhe::scene::Light>& light,
    const erhe::property::Dependency_property& property,
    const bool                                 degrees
)
{
    const erhe::property::Property_ui& ui = property.get_default_metadata().ui;
    const float scale = degrees ? glm::degrees(1.0f) : 1.0f;
    const float min   = ui.min.value_or(0.0f) * scale;
    const float max   = ui.max.value_or(1.0f) * scale;
    float value = std::get<float>(light->get_value(property)) * scale;

    ImGui::PushID(&property);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(light->is_write_sealed(property));
    ImGuiSliderFlags flags = ImGuiSliderFlags_NoRoundToFormat;
    if (ui.logarithmic) {
        flags |= ImGuiSliderFlags_Logarithmic;
    }
    const bool changed = ImGui::SliderFloat("##", &value, min, max, degrees ? "%.1f deg" : "%.3f", flags);
    ImGui::EndDisabled();
    track_edit(light, property, changed);
    if (changed) {
        light->set_value(property, erhe::property::make_value(value / scale));
    }
    ImGui::PopID();
}

void Light_mixer_window::color_button(const std::shared_ptr<erhe::scene::Light>& light)
{
    const erhe::property::Dependency_property& property = erhe::scene::Light::color_property.get();
    glm::vec3 color = light->get_color();
    ImGui::BeginDisabled(light->is_write_sealed(property));
    const bool changed = ImGui::ColorEdit3("##color", &color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_Float);
    ImGui::EndDisabled();
    track_edit(light, property, changed);
    if (changed) {
        light->set_value(property, erhe::property::make_value(color));
    }
}

// Live writes while a widget is dragged (as the Properties rows do); the
// completed edit is one undoable operation.
void Light_mixer_window::track_edit(
    const std::shared_ptr<erhe::scene::Light>& light,
    const erhe::property::Dependency_property& property,
    const bool                                 changed
)
{
    const bool this_edit = (m_edit_light == light) && (m_edit_property == &property);
    if (ImGui::IsItemActivated() || (changed && !this_edit)) {
        begin_edit(light, property); // keyboard edits can change without a separate activation frame
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        end_edit();
    } else if (this_edit && ImGui::IsItemDeactivated()) {
        m_edit_light.reset(); // activated, never edited
        m_edit_property = nullptr;
        m_edit_before.reset();
    }
}

void Light_mixer_window::begin_edit(const std::shared_ptr<erhe::scene::Light>& light, const erhe::property::Dependency_property& property)
{
    if (m_edit_light && ((m_edit_light != light) || (m_edit_property != &property))) {
        end_edit(); // a popup picker may not report its deactivation
    }
    m_edit_light    = light;
    m_edit_property = &property;
    m_edit_before   = light->read_local_state(property);
}

void Light_mixer_window::end_edit()
{
    if (!m_edit_light || (m_edit_property == nullptr)) {
        m_edit_light.reset();
        m_edit_property = nullptr;
        m_edit_before.reset();
        return;
    }
    const bool alive = (m_context.app_scenes != nullptr) && m_context.app_scenes->is_host_registered(m_edit_light->get_item_host());
    if (alive && (m_context.operation_stack != nullptr)) {
        m_context.operation_stack->queue(
            std::make_shared<Property_set_operation>(
                m_edit_light,
                *m_edit_property,
                std::move(m_edit_before),
                m_edit_light->read_local_state(*m_edit_property)
            )
        );
    }
    m_edit_light.reset();
    m_edit_property = nullptr;
    m_edit_before.reset();
}

} // namespace editor
