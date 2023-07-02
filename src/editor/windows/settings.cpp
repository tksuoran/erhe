#include "windows/settings.hpp"

#include "editor_context.hpp"
#include "editor_message_bus.hpp"

#include "erhe/configuration/configuration.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/imgui/imgui_helpers.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/scene_renderer/shadow_renderer.hpp"
#include "erhe/toolkit/verify.hpp"
#include "mini/ini.h"

#include <fmt/format.h>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

Settings_window::Settings_window(
    erhe::imgui::Imgui_renderer&           imgui_renderer,
    erhe::imgui::Imgui_windows&            imgui_windows,
    erhe::scene_renderer::Shadow_renderer& shadow_renderer,
    Editor_context&                        editor_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Settings", "settings"}
    , m_context                {editor_context}
{
    int num_sample_counts{0};
    gl::get_internalformat_iv(
        gl::Texture_target::texture_2d_multisample,
        gl::Internal_format::rgba16f,
        gl::Internal_format_p_name::num_sample_counts,
        1,
        &num_sample_counts
    );

    if (num_sample_counts > 0) {
        m_msaa_sample_count_entry_values.resize(num_sample_counts);
        m_msaa_sample_count_entry_s_strings.resize(num_sample_counts);
        m_msaa_sample_count_entry_strings.resize(num_sample_counts);
        gl::get_internalformat_iv(
            gl::Texture_target::texture_2d_multisample,
            gl::Internal_format::rgba16f,
            gl::Internal_format_p_name::samples,
            num_sample_counts,
            m_msaa_sample_count_entry_values.data()
        );
        std::sort(
            m_msaa_sample_count_entry_values.begin(),
            m_msaa_sample_count_entry_values.end()
        );
        for (std::size_t i = 0; i < num_sample_counts; ++i) {
            m_msaa_sample_count_entry_strings.at(i) = fmt::format("{}", m_msaa_sample_count_entry_values.at(i));
            m_msaa_sample_count_entry_s_strings.at(i) = m_msaa_sample_count_entry_strings.at(i).c_str();
        }
    }

    int max_depth_width{0};
    gl::get_internalformat_iv(
        gl::Texture_target::texture_2d_array,
        gl::Internal_format::depth_component32f,
        gl::Internal_format_p_name::max_width,
        1,
        &max_depth_width
    );
    int max_depth_height{0};
    gl::get_internalformat_iv(
        gl::Texture_target::texture_2d_array,
        gl::Internal_format::depth_component32f,
        gl::Internal_format_p_name::max_height,
        1,
        &max_depth_height
    );
    gl::get_internalformat_iv(
        gl::Texture_target::texture_2d_array,
        gl::Internal_format::depth_component32f,
        gl::Internal_format_p_name::max_layers,
        1,
        &m_max_depth_layers
    );
    m_max_shadow_resolution = std::min(max_depth_width, max_depth_height);

    read_ini();

    // Override configuration
    auto& shadow_config = shadow_renderer.config;
    for (std::size_t i = 0, end = m_settings.size(); i < end; ++i) {
        const auto& settings = m_settings.at(i);
        if (settings.name == m_used_settings) {
            shadow_config.enabled                    = settings.shadow_enable;
            shadow_config.shadow_map_resolution      = settings.shadow_resolution;
            shadow_config.shadow_map_max_light_count = settings.shadow_light_count;
            m_msaa_sample_count = settings.msaa_sample_count;
            m_settings_index = static_cast<int>(i);
            break;
        }
    }
}

void Settings_window::show_settings(Settings& settings)
{
    apply_limits(settings);
}

void Settings_window::apply_limits(Settings& settings)
{
    settings.shadow_resolution  = std::min(settings.shadow_resolution,  m_max_shadow_resolution);
    settings.shadow_light_count = std::min(settings.shadow_light_count, m_max_depth_layers);

    settings.shadow_resolution  = std::min(settings.shadow_resolution,  8192);
    settings.shadow_light_count = std::min(settings.shadow_light_count, 128);
}

void Settings_window::use_settings(const Settings& settings)
{
    m_used_settings = settings.name;

    auto& shadow_config = m_context.shadow_renderer->config;
    shadow_config.enabled                    = settings.shadow_enable;
    shadow_config.shadow_map_resolution      = settings.shadow_resolution;
    shadow_config.shadow_map_max_light_count = settings.shadow_light_count;
    m_msaa_sample_count = settings.msaa_sample_count;

    m_context.editor_message_bus->send_message(
        Editor_message{
            .update_flags = Message_flag_bit::c_flag_bit_graphics_settings
        }
    );
}

void Settings_window::read_ini()
{
    {
        mINI::INIFile file("presets.ini");
        mINI::INIStructure ini;
        if (file.read(ini)) {
            m_settings.clear();
            for (const auto& i : ini) {
                const auto& section = i.second;
                Settings settings;
                settings.name = section.get("name");
                erhe::configuration::ini_get(section, "shadow_enable",      settings.shadow_enable     );
                erhe::configuration::ini_get(section, "msaa_sample_count",  settings.msaa_sample_count );
                erhe::configuration::ini_get(section, "shadow_resolution",  settings.shadow_resolution );
                erhe::configuration::ini_get(section, "shadow_light_count", settings.shadow_light_count);
                apply_limits(settings);
                m_settings.push_back(settings);
            }
        }
    }

    {
        mINI::INIFile file("user.ini");
        mINI::INIStructure ini;
        if (file.read(ini)) {
            const auto& section = ini["user"];
            if (section.has("used_preset")) {
                m_used_settings = section.get("used_preset");
            }
        }
    }
}

void Settings_window::write_ini()
{
    {
        mINI::INIFile file("presets.ini");
        mINI::INIStructure ini;
        for (const auto& settings : m_settings) {
            ini[settings.name]["name"              ] = settings.name;
            ini[settings.name]["shadow_enable"     ] = fmt::format("{}", settings.shadow_enable     );
            ini[settings.name]["msaa_sample_count" ] = fmt::format("{}", settings.msaa_sample_count );
            ini[settings.name]["shadow_resolution" ] = fmt::format("{}", settings.shadow_resolution );
            ini[settings.name]["shadow_light_count"] = fmt::format("{}", settings.shadow_light_count);
        }
        file.generate(ini);
    }
    {
        mINI::INIFile file("user.ini");
        mINI::INIStructure ini;
        ini["user"]["used_preset"] = m_used_settings;
        file.generate(ini);
    }
}

void Settings_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)

    ImGui::Text("Used Preset: %s", m_used_settings.c_str());
    if (!m_settings.empty()) {
        m_settings_names.resize(m_settings.size());
        for (std::size_t i = 0, end = m_settings.size(); i < end; ++i) {
            m_settings_names.at(i) = m_settings.at(i).name.c_str();
        }

        ImGui::SetNextItemWidth(300);
        ImGui::Combo(
            "Edit Preset",
            &m_settings_index,
            m_settings_names.data(),
            static_cast<int>(m_settings_names.size())
        );
        show_settings(m_settings.at(m_settings_index));
    }

    ImGui::NewLine();

    if (!m_settings.empty()) {
        Settings& settings = m_settings.at(m_settings_index);

        ImGui::InputText("Preset Name", &settings.name);

        ImGui::Separator();

        for (
            m_msaa_sample_count_entry_index = 0;
            m_msaa_sample_count_entry_index < m_msaa_sample_count_entry_values.size();
            ++m_msaa_sample_count_entry_index
        ) {
            if (m_msaa_sample_count_entry_values[m_msaa_sample_count_entry_index] >= settings.msaa_sample_count) {
                break;
            }
        }
        m_msaa_sample_count_entry_index = std::min(
            m_msaa_sample_count_entry_index,
            static_cast<int>(m_msaa_sample_count_entry_values.size() - 1)
        );

        const bool msaa_changed = ImGui::Combo(
            "MSAA Sample Count",
            &m_msaa_sample_count_entry_index,
            m_msaa_sample_count_entry_s_strings.data(),
            static_cast<int>(m_msaa_sample_count_entry_s_strings.size())
        );
        if (msaa_changed) {
            settings.msaa_sample_count = m_msaa_sample_count_entry_values[m_msaa_sample_count_entry_index];
        }

        ImGui::Checkbox     ("Shadows Enabled", &settings.shadow_enable);
        ImGui::BeginDisabled(!settings.shadow_enable);
        ImGui::SliderInt    ("Shadow Resolution",  &settings.shadow_resolution,  1, m_max_shadow_resolution);
        ImGui::SliderInt    ("Shadow Light Count", &settings.shadow_light_count, 1, m_max_depth_layers);
        ImGui::EndDisabled  ();

        // ImGui::BeginDisabled(!erhe::graphics::g_instance->info.use_bindless_texture);
        // ImGui::Checkbox     ("Bindless Textures", &settings.bindless_textures);
        // ImGui::EndDisabled  ();

        ImGui::NewLine();
    }

    constexpr ImVec2 button_size{110.0f, 0.0f};

    const bool add_pressed = ImGui::Button("Add Preset", button_size);
    if (add_pressed || m_settings.empty()) {
        Settings new_settings{
            .name = "New preset"
        };
        m_settings.push_back(new_settings);
        m_settings_index = static_cast<int>(m_settings.size() - 1);
    }

    ImGui::SameLine();
    if (ImGui::Button("Remove Preset", button_size)) {
        m_settings.erase(m_settings.begin() + m_settings_index);
    }

    ImGui::SameLine();
    if (ImGui::Button("Use Preset", button_size)) {
        use_settings(m_settings.at(m_settings_index));
    }

    ImGui::Separator();
    if (ImGui::Button("Load Presets", button_size)) {
        read_ini();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Presets", button_size)) {
        write_ini();
    }
#endif
}

} // namespace editor
