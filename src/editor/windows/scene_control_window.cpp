#include "windows/scene_control_window.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "scene/headlamp.hpp"
#include "tools/fly_camera_tool.hpp"
#include "config/generated/graphics_preset_entry.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/culling_classification.hpp"
#include "config/generated/light_cluster_binning.hpp"
#include "renderers/content_draws.hpp"
#include "windows/config_ui.hpp"
#include "config/generated/hud_config.hpp"
#include "config/generated/hud_config_serialization.hpp"

#include <cstring>

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#else
#   include "xr/null_headset_view.hpp"
#endif

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_profile/profile.hpp"
#include "config/generated/light_shading_mode.hpp"
#include "erhe_scene_renderer/cull_classification.hpp"

#include <imgui/imgui.h>

#include <optional>
#include <string>
#include <vector>

namespace editor {

void queue_session_preset_edit(App_context& context, std::function<void(Graphics_preset_entry&)> edit)
{
    context.imgui_windows->queue(
        [&context, edit = std::move(edit)]() {
            if ((context.app_settings == nullptr) || (context.app_message_bus == nullptr)) {
                return;
            }
            Graphics_settings&    graphics = context.app_settings->graphics;
            Graphics_preset_entry preset   = graphics.current_graphics_preset;
            edit(preset);
            graphics.apply_limits(preset);
            graphics.apply_preset(preset, *context.app_message_bus);
        }
    );
}

void set_headlamp_only_lighting(Graphics_preset_entry& preset, const bool cast_shadow)
{
    preset.light_shading                      = Light_shading_mode::loops;
    preset.shadow_enable                      = true;
    preset.directional_shadow_light_count     = 0;
    preset.directional_unshadowed_light_count = 0;
    preset.spot_shadow_light_count            = cast_shadow ? 1 : 0;
    preset.spot_unshadowed_light_count        = cast_shadow ? 0 : 1;
    preset.point_shadow_light_count           = 0;
    preset.point_unshadowed_light_count       = 0;
}

auto is_headlamp_only_lighting(const Graphics_preset_entry& preset) -> bool
{
    return
        (preset.light_shading == Light_shading_mode::loops) &&
        (preset.directional_shadow_light_count == 0) && (preset.directional_unshadowed_light_count == 0) &&
        ((preset.spot_shadow_light_count + preset.spot_unshadowed_light_count) == 1) &&
        (preset.point_shadow_light_count == 0) && (preset.point_unshadowed_light_count == 0);
}

Scene_control_window::Scene_control_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Scene Control", "scene_control", true}
    , m_context{context}
{
}

void Scene_control_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    // The headset's own controls (status line, headset scene, its view
    // toolbar, headlamp, controller ray) need an OpenXR session; without one
    // the headlamp rides on the fly camera's camera.
    if (m_context.OpenXR && (m_context.headset_view != nullptr)) {
        m_context.headset_view->scene_control_imgui(get_imgui_host());
    } else if (m_context.fly_camera_tool != nullptr) {
        m_context.fly_camera_tool->get_headlamp().imgui(
            "A spot light 15 cm above the camera the Fly Camera controls (the viewport's camera, chosen in its toolbar), "
            "aimed along the view (range 30 m); the first spot light of the scene"
        );
    }
    lighting_and_lod_imgui();
    culling_imgui();
    hud_placement_imgui();
}

void Scene_control_window::hud_placement_imgui()
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    // The reflected Hud_config fields (the Settings window shows the whole
    // struct). Read at the next show of the Hud: re-placing the visible Hud
    // while its own slider is dragged would move it under the user.
    Hud_config& hud = m_context.editor_settings->hud;
    ImGui::SeparatorText("Hud Placement");
    constexpr const char* field_names[] = { "summon_distance", "summon_offset_x", "summon_offset_y", "summon_offset_z" };
    for (const erhe::codegen::Field_info& field : get_fields(static_cast<const Hud_config*>(nullptr))) {
        bool shown = false;
        for (const char* name : field_names) {
            shown = shown || (std::strcmp(field.name, name) == 0);
        }
        if (!shown) {
            continue;
        }
        ImGui::PushID(field.name);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        static_cast<void>(imgui_field(&hud, field));
        ImGui::SameLine();
        ImGui::TextUnformatted(field.short_desc);
        ImGui::SetItemTooltip("%s", field.long_desc);
        ImGui::PopID();
    }
}

void Scene_control_window::culling_imgui()
{
    if ((m_context.editor_settings == nullptr) || (m_context.app_settings == nullptr)) {
        return;
    }
    // The same stored settings the Settings window edits; the render
    // settings snapshot carries each value to its consumer.
    Editor_settings_config& settings     = *m_context.editor_settings;
    App_settings&           app_settings = *m_context.app_settings;
    ImGui::SeparatorText("Culling");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    static_cast<void>(imgui_enum_combo("Culling Classification", settings.culling_classification));
    ImGui::SetItemTooltip("Where the draw lists' culling classification runs: the CPU, or also a compute pass on the GPU that generates the draw commands (devices with a GPU-read draw count; others use the CPU).");
    if (m_context.OpenXR) {
        static_cast<void>(ImGui::Checkbox("Headset Culling", &app_settings.headset_culling_session));
        ImGui::SetItemTooltip("Frustum-cull the headset's color passes against the union of the eye frusta. Session only.");
    }
    static_cast<void>(ImGui::Checkbox("Occlusion Culling", &settings.occlusion_culling));
    ImGui::SetItemTooltip("Raster occlusion test (gpu culling classification only): the entries' boxes are drawn against each view's opaque depth; the next frame skips the entries found hidden.");
    static_cast<void>(ImGui::Checkbox("PVS Culling", &settings.pvs_culling));
    ImGui::SetItemTooltip("Cull the entries outside the potentially visible set of the cells around each camera, when the scene has a ready PVS database. A stale or absent database culls nothing.");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    static_cast<void>(ImGui::DragFloat("PVS Query Margin", &settings.pvs_query_margin, 0.005f, 0.0f, 10.0f, "%.3f m"));
    ImGui::SetItemTooltip("Half the size of the box around a camera whose cells' potentially visible sets the PVS culling reads.");
    {
        Content_draws& content_draws = app_settings.content_draws_session;
        int            index         = static_cast<int>(content_draws);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        if (ImGui::Combo("Content Draws", &index, c_content_draws_strings, IM_ARRAYSIZE(c_content_draws_strings))) {
            content_draws = static_cast<Content_draws>(index);
        }
        ImGui::SetItemTooltip("All content draws, or opaque only (no alpha-tested or translucent materials), to measure what they cost. Session only.");
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    static_cast<void>(imgui_enum_combo("Light Cluster Binning", settings.light_cluster_binning));
    ImGui::SetItemTooltip("Where the light cluster lists of clustered light shading are binned: a compute pass on the GPU, or the CPU on the render thread.");
}

void Scene_control_window::lighting_and_lod_imgui()
{
    if (m_context.app_settings == nullptr) {
        return;
    }
    const Graphics_preset_entry& preset = m_context.app_settings->graphics.current_graphics_preset;

    // Lighting: the scene's lights with clustered shading, or one shadowed
    // directional light (the headlamp when enabled: it is the first
    // directional light) with every other light unshaded. Session only.
    {
        const bool single_directional =
            (preset.light_shading == Light_shading_mode::loops) &&
            (preset.directional_shadow_light_count == 1) && (preset.directional_unshadowed_light_count == 0) &&
            (preset.spot_shadow_light_count        == 0) && (preset.spot_unshadowed_light_count        == 0) &&
            (preset.point_shadow_light_count       == 0) && (preset.point_unshadowed_light_count       == 0);
        const bool clustered     = (preset.light_shading == Light_shading_mode::clustered);
        const bool headlamp_only = is_headlamp_only_lighting(preset);
        const char* const preview =
            single_directional ? "Single directional light" :
            headlamp_only      ? "Headlamp only" :
            clustered          ? "Scene lights (clustered)" :
                                 "Preset lighting";
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        if (ImGui::BeginCombo("Lighting", preview)) {
            if (ImGui::Selectable("Scene lights (clustered)", clustered && !single_directional)) {
                // The stored preset's light counts (shadow slots), clustered.
                const std::vector<Graphics_preset_entry>& stored = m_context.app_settings->graphics.graphics_presets;
                const std::string name = preset.name;
                std::optional<Graphics_preset_entry> stored_preset;
                for (const Graphics_preset_entry& entry : stored) {
                    if (entry.name == name) {
                        stored_preset = entry;
                    }
                }
                queue_session_preset_edit(
                    m_context,
                    [stored_preset](Graphics_preset_entry& edited) {
                        if (stored_preset.has_value()) {
                            edited.directional_shadow_light_count     = stored_preset->directional_shadow_light_count;
                            edited.directional_unshadowed_light_count = stored_preset->directional_unshadowed_light_count;
                            edited.spot_shadow_light_count            = stored_preset->spot_shadow_light_count;
                            edited.spot_unshadowed_light_count        = stored_preset->spot_unshadowed_light_count;
                            edited.point_shadow_light_count           = stored_preset->point_shadow_light_count;
                            edited.point_unshadowed_light_count       = stored_preset->point_unshadowed_light_count;
                        }
                        edited.light_shading = Light_shading_mode::clustered;
                    }
                );
            }
            if (ImGui::Selectable("Single directional light", single_directional)) {
                queue_session_preset_edit(
                    m_context,
                    [](Graphics_preset_entry& edited) {
                        edited.light_shading                      = Light_shading_mode::loops;
                        edited.shadow_enable                      = true;
                        edited.directional_shadow_light_count     = 1;
                        edited.directional_unshadowed_light_count = 0;
                        edited.spot_shadow_light_count            = 0;
                        edited.spot_unshadowed_light_count        = 0;
                        edited.point_shadow_light_count           = 0;
                        edited.point_unshadowed_light_count       = 0;
                    }
                );
            }
            // The headset's headlamp under OpenXR, else the fly camera's.
            const bool headset_headlamp = m_context.OpenXR && (m_context.headset_view != nullptr);
            const bool has_headlamp     = headset_headlamp || (m_context.fly_camera_tool != nullptr);
            if (has_headlamp && ImGui::Selectable("Headlamp only", headlamp_only)) {
                const bool cast_shadow = headset_headlamp
                    ? m_context.headset_view->get_headlamp_cast_shadow()
                    : m_context.fly_camera_tool->get_headlamp().get_cast_shadow();
                queue_session_preset_edit(m_context, [cast_shadow](Graphics_preset_entry& edited) { set_headlamp_only_lighting(edited, cast_shadow); });
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Session only (graphics_presets.json is not written). Single directional light: the first directional light is the only light. Headlamp only: one spot light, the headlamp (the first spot light), is the only light.");
    }

    // Level of detail selection (doc/editor/lod.md). Session only.
    {
        bool lod_enabled = preset.lod_enabled;
        if (ImGui::Checkbox("LOD Selection", &lod_enabled)) {
            queue_session_preset_edit(m_context, [lod_enabled](Graphics_preset_entry& edited) { edited.lod_enabled = lod_enabled; });
        }
        ImGui::SetItemTooltip("Automatic level of detail selection; off draws every mesh at level 0 (the bias then does nothing)");
        float threshold = preset.lod_threshold_pixels;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        if (ImGui::SliderFloat("LOD Threshold (pixels)", &threshold, 0.125f, 512.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) {
            queue_session_preset_edit(m_context, [threshold](Graphics_preset_entry& edited) { edited.lod_threshold_pixels = threshold; });
        }
        int bias = preset.lod_bias;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        if (ImGui::SliderInt("LOD Bias", &bias, -7, 7)) {
            queue_session_preset_edit(m_context, [bias](Graphics_preset_entry& edited) { edited.lod_bias = bias; });
        }
        ImGui::SetItemTooltip("Added to every selected level, then clamped to each mesh's chain: +N draws every mesh at least N levels coarser (as far as its chain goes)");
        float band = preset.lod_hysteresis_band;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        if (ImGui::SliderFloat("LOD Hysteresis Band", &band, 0.0f, erhe::scene_renderer::max_lod_hysteresis_band, "%.2f")) {
            queue_session_preset_edit(m_context, [band](Graphics_preset_entry& edited) { edited.lod_hysteresis_band = band; });
        }
    }
}

} // namespace editor
