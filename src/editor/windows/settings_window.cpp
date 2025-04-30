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

namespace editor {

Settings_window::Settings_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Settings", "settings"}
    , m_context                {editor_context}
{
}

void Settings_window::rasterize_icons()
{
    Icons icons;
    Icon_loader icon_loader{m_context.editor_settings->icon_settings};
    icons.queue_load_icons(icon_loader);
    icon_loader.execute_rasterization_queue();

    m_context.icon_set->load_icons(
        *m_context.graphics_instance,
        m_context.editor_settings->icon_settings,
        icons,
        icon_loader
    );
}

void Settings_window::imgui()
{
    reset();

    const float ui_scale = m_context.editor_settings->get_ui_scale();
    const ImVec2 button_size{110.0f * ui_scale, 0.0f};

    push_group("User Interface", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen);
    add_entry("UI Font Size", [this](){
        auto& imgui = m_context.editor_settings->imgui;
        const bool font_size_changed = ImGui::DragFloat("UI Font Size", &imgui.font_size, 0.1f, 4.0f, 100.0f, "%.1f");
        if (font_size_changed) {
            m_context.imgui_renderer->on_font_config_changed(imgui);
        }
    });

    add_entry("Small Icon Size", [this](){
        int* icon_size = &m_context.editor_settings->icon_settings.small_icon_size;
        const int old_icon_size = *icon_size;
        ImGui::DragInt("##", icon_size, 0.1f, 4, 512);
        if (ImGui::IsItemDeactivatedAfterEdit() && old_icon_size != *icon_size) {
            rasterize_icons();
        }
    });
    add_entry("Large Icon Size", [this](){
        int* icon_size = &m_context.editor_settings->icon_settings.large_icon_size;
        const int old_icon_size = *icon_size;
        ImGui::DragInt("##", icon_size, 0.1f, 4, 512);
        if (ImGui::IsItemDeactivatedAfterEdit() && old_icon_size != *icon_size) {
            rasterize_icons();
        }
    });

    add_entry("Hotbar Icon Size", [this](){
        int* icon_size = &m_context.editor_settings->icon_settings.hotbar_icon_size;
        const int old_icon_size = *icon_size;
        ImGui::DragInt("##", icon_size, 0.1f, 4, 512);
        if (ImGui::IsItemDeactivatedAfterEdit() && old_icon_size != *icon_size) {
            rasterize_icons();
        }
    });

    pop_group();

    push_group("Graphics", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen);

    auto& graphics = m_context.editor_settings->graphics;
    add_entry("Currently Used Preset", [&graphics]() {
        ImGui::TextUnformatted(graphics.current_graphics_preset.name.c_str());
    });
    
    auto& graphics_presets = graphics.graphics_presets;
    if (!graphics_presets.empty()) {
        m_graphics_preset_names.resize(graphics_presets.size());
        for (std::size_t i = 0, end = graphics_presets.size(); i < end; ++i) {
            m_graphics_preset_names.at(i) = graphics_presets.at(i).name.c_str();
        }

        add_entry("Edit", [this](){
            ImGui::Combo(
                "##edit_preset",
                &m_graphics_preset_index,
                m_graphics_preset_names.data(),
                static_cast<int>(m_graphics_preset_names.size())
            );
        });
        //// show_settings(graphics_presets.at(m_graphics_preset_index));
    }

    ImGui::NewLine();

    if (!graphics_presets.empty()) {
        Graphics_preset& graphics_preset = graphics_presets.at(m_graphics_preset_index);
        add_entry("Preset Name", [&graphics_preset](){
            ImGui::InputText("##", &graphics_preset.name);
        });

        add_entry("MSAA Sample Count", [this, &graphics, &graphics_preset](){
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
                "##",
                &m_msaa_sample_count_entry_index,
                graphics.msaa_sample_count_entry_s_strings.data(),
                static_cast<int>(graphics.msaa_sample_count_entry_s_strings.size())
            );
            if (msaa_changed) {
                graphics_preset.msaa_sample_count = msaa_sample_count_entry_values[m_msaa_sample_count_entry_index];
            }
        });

        add_entry("Shadows Enabled", [&graphics_preset]() {
            ImGui::Checkbox("##", &graphics_preset.shadow_enable);
        });
        add_entry("Shadow Resolution", [this, &graphics_preset](){
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
                "##",
                &m_shadow_resolution_index,
                shadow_resolution_items,
                IM_ARRAYSIZE(shadow_resolution_items),
                IM_ARRAYSIZE(shadow_resolution_items)
            )) {
                graphics_preset.shadow_resolution = shadow_resolution_values[m_shadow_resolution_index];
            }
        });

        //ImGui::SliderInt  ("Shadow Resolution",  &graphics_preset.shadow_resolution,  1, graphics.max_shadow_resolution);
        add_entry("Shadow Light Count", [&graphics, &graphics_preset](){
            ImGui::SliderInt("##", &graphics_preset.shadow_light_count, 1, std::min(graphics.max_depth_layers, 20));
        });
    }

    add_entry("", [this, button_size, &graphics_presets](){
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
    });
    pop_group();

    add_entry("", [this, button_size](){
        if (ImGui::Button("Load", button_size)) {
            m_context.editor_settings->read();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save", button_size)) {
            m_context.editor_settings->write();
        }
    });

    show_entries("Settings", ImVec2{1.0f, 1.0f});
}

} // namespace editor
