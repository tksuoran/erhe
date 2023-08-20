#include "windows/settings_window.hpp"

#include "editor_context.hpp"
#include "editor_message_bus.hpp"
#include "editor_settings.hpp"
#include "graphics/icon_set.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

Settings_window::Settings_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Settings", "settings"}
    , m_context                {editor_context}
{
}

void Settings_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const float ui_scale = m_context.editor_settings->get_ui_scale();
    const ImVec2 button_size{110.0f * ui_scale, 0.0f};

    if (ImGui::TreeNodeEx("User Interface", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& imgui = m_context.editor_settings->imgui;
        auto& icons = m_context.editor_settings->icons;
        const bool font_size_changed = ImGui::DragFloat(
            "UI Font Size",
            &imgui.font_size,
            0.1f,
            4.0f,
            100.0f,
            "%.1f"
        );
        if (font_size_changed) {
            m_context.imgui_renderer->on_font_config_changed(imgui);
        }

        ImGui::DragInt(
            "Small Icon Size",
            &icons.small_icon_size,
            0.1f,
            4,
            512
        );
        const bool small_icon_size_changed = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragInt(
            "Large Icon Size",
            &icons.large_icon_size,
            0.1f,
            4,
            512
        );
        const bool large_icon_size_changed = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragInt(
            "Hotbar Icon Size",
            &icons.hotbar_icon_size,
            0.1f,
            4,
            512
        );
        const bool hotbar_icon_size_changed = ImGui::IsItemDeactivatedAfterEdit();

        if (small_icon_size_changed || large_icon_size_changed || hotbar_icon_size_changed) {
            m_context.icon_set->load_icons(
                *m_context.graphics_instance,
                *m_context.imgui_renderer,
                icons,
                *m_context.programs
            );
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Graphics", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& graphics = m_context.editor_settings->graphics;
        ImGui::Text("Currently Used Preset: %s", graphics.current_graphics_preset.name.c_str());
        ImGui::SeparatorText("Edit Preset");
        auto& graphics_presets = graphics.graphics_presets;
        if (!graphics_presets.empty()) {
            m_graphics_preset_names.resize(graphics_presets.size());
            for (std::size_t i = 0, end = graphics_presets.size(); i < end; ++i) {
                m_graphics_preset_names.at(i) = graphics_presets.at(i).name.c_str();
            }

            ImGui::SetNextItemWidth(300);
            ImGui::Combo(
                "##edit_preset",
                &m_graphics_preset_index,
                m_graphics_preset_names.data(),
                static_cast<int>(m_graphics_preset_names.size())
            );
            //// show_settings(graphics_presets.at(m_graphics_preset_index));
        }

        ImGui::NewLine();

        if (!graphics_presets.empty()) {
            Graphics_preset& graphics_preset = graphics_presets.at(m_graphics_preset_index);
            ImGui::InputText("Preset Name", &graphics_preset.name);
            ImGui::Separator();

            auto& msaa_sample_count_entry_values = graphics.msaa_sample_count_entry_values;
            for (
                m_msaa_sample_count_entry_index = 0;
                m_msaa_sample_count_entry_index < msaa_sample_count_entry_values.size();
                ++m_msaa_sample_count_entry_index
            ) {
                if (msaa_sample_count_entry_values[m_msaa_sample_count_entry_index] >= graphics_preset.msaa_sample_count) {
                    break;
                }
            }
            m_msaa_sample_count_entry_index = std::min(
                m_msaa_sample_count_entry_index,
                static_cast<int>(msaa_sample_count_entry_values.size() - 1)
            );

            const bool msaa_changed = ImGui::Combo(
                "MSAA Sample Count",
                &m_msaa_sample_count_entry_index,
                graphics.msaa_sample_count_entry_s_strings.data(),
                static_cast<int>(graphics.msaa_sample_count_entry_s_strings.size())
            );
            if (msaa_changed) {
                graphics_preset.msaa_sample_count = msaa_sample_count_entry_values[m_msaa_sample_count_entry_index];
            }

            ImGui::Checkbox     ("Shadows Enabled", &graphics_preset.shadow_enable);
            ImGui::BeginDisabled(!graphics_preset.shadow_enable);

            const int   shadow_resolution_values[] = {  256, 512, 1024, 1024 * 2, 1024 * 3, 1024 * 4, 1024 * 5, 1024 * 6, 1024 * 7, 1024 * 8 };
            const char* shadow_resolution_items [] = { "256", "512", "1024", "2048", "3072", "4096", "5120", "6144", "7168", "8192" };
            m_shadow_resolution_index = 0;
            int min_distance = std::numeric_limits<int>::max();
            for (int i = 0, end = IM_ARRAYSIZE(shadow_resolution_values); i < end; ++i) {
                int entry_distance = std::abs(shadow_resolution_values[i] - graphics_preset.shadow_resolution);
                if (entry_distance < min_distance) {
                    m_shadow_resolution_index = i;
                    min_distance = entry_distance;
                }
            }
            if (ImGui::Combo(
                "Shadow Resolution",
                &m_shadow_resolution_index,
                shadow_resolution_items,
                IM_ARRAYSIZE(shadow_resolution_items),
                IM_ARRAYSIZE(shadow_resolution_items)
            )) {
                graphics_preset.shadow_resolution = shadow_resolution_values[m_shadow_resolution_index];
            }

            //ImGui::SliderInt  ("Shadow Resolution",  &graphics_preset.shadow_resolution,  1, graphics.max_shadow_resolution);
            ImGui::SliderInt  ("Shadow Light Count", &graphics_preset.shadow_light_count, 1, graphics.max_depth_layers);
            ImGui::EndDisabled();

            // ImGui::BeginDisabled(!erhe::graphics::g_instance->info.use_bindless_texture);
            // ImGui::Checkbox     ("Bindless Textures", &settings.bindless_textures);
            // ImGui::EndDisabled  ();

            ImGui::NewLine();
        }

        const bool add_pressed = ImGui::Button("Add", button_size);
        if (add_pressed || graphics_presets.empty()) {
            Graphics_preset new_graphics_preset{
                .name = "New preset"
            };
            graphics_presets.push_back(new_graphics_preset);
            m_graphics_preset_index = static_cast<int>(graphics_presets.size() - 1);
        }

        ImGui::SameLine();
        if (ImGui::Button("Remove", button_size)) {
            graphics_presets.erase(graphics_presets.begin() + m_graphics_preset_index);
        }

        ImGui::SameLine();
        if (ImGui::Button("Use", button_size)) {
            m_context.editor_settings->graphics.current_graphics_preset = graphics_presets.at(m_graphics_preset_index);
            m_context.editor_message_bus->send_message(
                Editor_message{
                    .update_flags    = Message_flag_bit::c_flag_bit_graphics_settings,
                    .graphics_preset = &m_context.editor_settings->graphics.current_graphics_preset
                }
            );
        }
        ImGui::TreePop();
    }

    ImGui::Separator();
    if (ImGui::Button("Load", button_size)) {
        m_context.editor_settings->read();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", button_size)) {
        m_context.editor_settings->write();
    }
#endif
}

} // namespace editor
