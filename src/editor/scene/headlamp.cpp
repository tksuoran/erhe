#include "scene/headlamp.hpp"

#include "app_context.hpp"
#include "app_message.hpp"
#include "app_settings.hpp"
#include "config/generated/graphics_preset_entry.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"
#include "windows/scene_control_window.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>

#include <algorithm>

namespace editor {

Headlamp::Headlamp(App_context& context)
    : m_context{context}
{
}

auto Headlamp::get_scene_root() const -> Scene_root*
{
    const std::shared_ptr<erhe::scene::Node> camera_node = m_camera_node.lock();
    return camera_node ? static_cast<Scene_root*>(camera_node->node_data.host) : nullptr;
}

void Headlamp::enable()
{
    if (m_enabled) {
        return;
    }
    m_enabled = true;
    if (!m_light) {
        m_light = std::make_shared<erhe::scene::Light>("Headlamp");
        m_light->set_light_type      (erhe::scene::Light::Type::spot);
        m_light->set_color           (glm::vec3{1.0f, 1.0f, 1.0f});
        m_light->set_range           (30.0f);
        m_light->set_inner_spot_angle(m_inner_spot_angle);
        m_light->set_outer_spot_angle(m_outer_spot_angle);
        m_light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::session_only | erhe::Item_flags::exclude_from_prefab);
        log_scene->info("Headlamp added");
    }
    m_light->set_intensity  (m_intensity);
    m_light->set_cast_shadow(m_cast_shadow);
    reattach();
    log_scene->info("Headlamp enabled (intensity {})", m_intensity);
}

void Headlamp::disable()
{
    if (!m_enabled) {
        return;
    }
    m_enabled = false;
    if (m_light) {
        // Leaving the scene unregisters the light (the light sets resolve again).
        m_light->set_parent(std::shared_ptr<erhe::scene::Node>{});
    }
    log_scene->info("Headlamp disabled");
}

void Headlamp::attach(const std::shared_ptr<erhe::scene::Node>& camera_node, const glm::mat4& parent_from_light)
{
    m_camera_node       = camera_node;
    m_parent_from_light = parent_from_light;
    reattach();
}

void Headlamp::detach()
{
    m_camera_node.reset();
    if (m_light) {
        m_light->set_parent(std::shared_ptr<erhe::scene::Node>{});
    }
}

void Headlamp::on_close_scene(const Scene_root& scene_root)
{
    if (get_scene_root() == &scene_root) {
        detach();
    }
}

void Headlamp::on_items_removed(const Removed_items& removed)
{
    if (m_light && removed.lookup.contains(m_light.get())) {
        // Deleted by the user: the undo history owns that light now; Add makes a new one.
        m_light->set_parent(std::shared_ptr<erhe::scene::Node>{});
        m_light.reset();
        m_enabled = false;
        return;
    }
    const std::shared_ptr<erhe::scene::Node> camera_node = m_camera_node.lock();
    if (camera_node && removed.lookup.contains(camera_node.get())) {
        detach();
    }
}

void Headlamp::reattach()
{
    const std::shared_ptr<erhe::scene::Node> camera_node = m_camera_node.lock();
    if (!m_enabled || !m_light || !camera_node) {
        return;
    }
    Scene_root* const scene_root = static_cast<Scene_root*>(camera_node->node_data.host);
    if (scene_root == nullptr) {
        return;
    }
    erhe::scene::Light_layer* const light_layer = scene_root->layers().light();
    ERHE_VERIFY(light_layer != nullptr);
    m_light->layer_id = light_layer->id;
    // The light looks down the camera's -Z, the view direction.
    m_light->set_parent(camera_node);
    m_light->set_parent_from_node(m_parent_from_light);
    {
        // The light set resolves lights[] in the prepare phase under this mutex.
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root->item_host_mutex};
        const bool moved = light_layer->move_to_front(m_light);
        ERHE_VERIFY(moved);
    }
    // The light order changed: the light sets resolve again.
    scene_root->on_light_changed(m_light);
}

void Headlamp::set_intensity(const float intensity)
{
    m_intensity = std::max(intensity, 0.0f);
    if (m_light) {
        m_light->set_intensity(m_intensity);
    }
}

void Headlamp::set_spot_angles(const float inner_spot_angle, const float outer_spot_angle)
{
    m_outer_spot_angle = std::max(outer_spot_angle, 0.0f);
    m_inner_spot_angle = std::clamp(inner_spot_angle, 0.0f, m_outer_spot_angle);
    if (m_light) {
        m_light->set_inner_spot_angle(m_inner_spot_angle);
        m_light->set_outer_spot_angle(m_outer_spot_angle);
    }
}

void Headlamp::imgui(const char* const tooltip)
{
    const char* const label = m_enabled ? "Disable Headlamp" : (m_light ? "Enable Headlamp" : "Add Headlamp");
    if (ImGui::Button(label)) {
        // A scene graph edit: queued to the prepare phase.
        if (m_enabled) {
            m_context.imgui_windows->queue([this]() { disable(); });
        } else {
            m_context.imgui_windows->queue([this]() { enable(); });
        }
    }
    ImGui::SetItemTooltip("%s", tooltip);
    float intensity = m_intensity;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::SliderFloat("Headlamp Intensity", &intensity, 0.0f, 20000.0f, "%.0f", ImGuiSliderFlags_Logarithmic)) {
        set_intensity(intensity);
    }
    constexpr float intensity_shortcuts[] = { 100.0f, 250.0f, 500.0f, 1000.0f, 2500.0f, 5000.0f };
    for (const float shortcut : intensity_shortcuts) {
        ImGui::PushID(static_cast<int>(shortcut));
        if (ImGui::Button(fmt::format("{:g}", shortcut).c_str())) {
            set_intensity(shortcut);
        }
        ImGui::PopID();
        ImGui::SameLine();
    }
    float inner_angle = m_inner_spot_angle;
    float outer_angle = m_outer_spot_angle;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    const bool inner_changed = ImGui::SliderAngle("Headlamp Cone Min", &inner_angle, 0.0f, 179.0f);
    ImGui::SetItemTooltip("Full cone angle where the falloff starts (inner spot angle)");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    const bool outer_changed = ImGui::SliderAngle("Headlamp Cone Max", &outer_angle, 0.0f, 179.0f);
    ImGui::SetItemTooltip("Full cone angle where the light ends (outer spot angle)");
    if (outer_changed) {
        set_spot_angles(std::min(inner_angle, outer_angle), outer_angle);
    } else if (inner_changed) {
        set_spot_angles(inner_angle, std::max(inner_angle, outer_angle));
    }
    bool cast_shadow = m_cast_shadow;
    if (ImGui::Checkbox("Headlamp Shadows", &cast_shadow)) {
        m_cast_shadow = cast_shadow;
        if (m_light) {
            m_light->set_cast_shadow(cast_shadow);
        }
        // "Headlamp only" lighting keeps the one spot slot the headlamp
        // takes (shadow-mapped or not).
        const Graphics_preset_entry& preset = m_context.app_settings->graphics.current_graphics_preset;
        if (is_headlamp_only_lighting(preset)) {
            queue_session_preset_edit(m_context, [cast_shadow](Graphics_preset_entry& edited) { set_headlamp_only_lighting(edited, cast_shadow); });
        }
    }
}

}
