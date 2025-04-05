#include "editor_settings.hpp"
#include "editor_message_bus.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

namespace editor {

Editor_settings::Editor_settings()
{
   read();
}

void Editor_settings::apply_limits(Editor_message_bus& editor_message_bus)
{
    graphics.get_limits();
    read();
    graphics.select_active_graphics_preset(editor_message_bus);
}

auto Editor_settings::get_ui_scale() const -> float
{
    return imgui.font_size / 16.0f;
}

void Graphics_settings::get_limits()
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
        msaa_sample_count_entry_values   .resize(num_sample_counts);
        msaa_sample_count_entry_s_strings.resize(num_sample_counts);
        msaa_sample_count_entry_strings  .resize(num_sample_counts);
        gl::get_internalformat_iv(
            gl::Texture_target::texture_2d_multisample,
            gl::Internal_format::rgba16f,
            gl::Internal_format_p_name::samples,
            num_sample_counts,
            msaa_sample_count_entry_values.data()
        );
        std::sort(
            msaa_sample_count_entry_values.begin(),
            msaa_sample_count_entry_values.end()
        );
        for (std::size_t i = 0; i < num_sample_counts; ++i) {
            msaa_sample_count_entry_strings  .at(i) = fmt::format("{}", msaa_sample_count_entry_values.at(i));
            msaa_sample_count_entry_s_strings.at(i) = msaa_sample_count_entry_strings.at(i).c_str();
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
        &max_depth_layers
    );
    max_shadow_resolution = std::min(max_depth_width, max_depth_height);
}

void Graphics_settings::read_presets()
{
    graphics_presets.clear();
    mINI::INIFile file("graphics_presets.ini");
    mINI::INIStructure ini;
    if (file.read(ini)) {
        for (const auto& i : ini) {
            const auto& section = i.second;
            Graphics_preset graphics_preset;
            graphics_preset.name = section.get("name");
            erhe::configuration::ini_get(section, "msaa_sample_count",  graphics_preset.msaa_sample_count );
            erhe::configuration::ini_get(section, "shadow_enable",      graphics_preset.shadow_enable     );
            erhe::configuration::ini_get(section, "shadow_resolution",  graphics_preset.shadow_resolution );
            erhe::configuration::ini_get(section, "shadow_light_count", graphics_preset.shadow_light_count);
            apply_limits(graphics_preset);
            graphics_presets.push_back(graphics_preset);
        }
    }
}

void Graphics_settings::write_presets()
{
    mINI::INIFile file("graphics_presets.ini");
    mINI::INIStructure ini;
    for (const auto& graphics_preset : graphics_presets) {
        ini[graphics_preset.name]["name"              ] = graphics_preset.name;
        ini[graphics_preset.name]["msaa_sample_count" ] = fmt::format("{}", graphics_preset.msaa_sample_count );
        ini[graphics_preset.name]["shadow_enable"     ] = fmt::format("{}", graphics_preset.shadow_enable     );
        ini[graphics_preset.name]["shadow_resolution" ] = fmt::format("{}", graphics_preset.shadow_resolution );
        ini[graphics_preset.name]["shadow_light_count"] = fmt::format("{}", graphics_preset.shadow_light_count);
    }
    file.generate(ini);
}

void Graphics_settings::apply_limits(Graphics_preset& graphics_preset)
{
    graphics_preset.shadow_resolution  = std::min(graphics_preset.shadow_resolution,  max_shadow_resolution);
    graphics_preset.shadow_light_count = std::min(graphics_preset.shadow_light_count, max_depth_layers);

    graphics_preset.shadow_resolution  = std::min(graphics_preset.shadow_resolution,  8192);
    graphics_preset.shadow_light_count = std::min(graphics_preset.shadow_light_count, 128);
}

void Graphics_settings::select_active_graphics_preset(Editor_message_bus& editor_message_bus)
{
    // Override configuration
    for (std::size_t i = 0, end = graphics_presets.size(); i < end; ++i) {
        const auto& graphics_preset = graphics_presets.at(i);
        if (graphics_preset.name == current_graphics_preset.name) {
            current_graphics_preset = graphics_preset;
            // Queue instead of instant send
            editor_message_bus.queue_message(
                Editor_message{
                    .update_flags    = Message_flag_bit::c_flag_bit_graphics_settings,
                    .graphics_preset = &current_graphics_preset
                }
            );
            break;
        }
    }
}

void Editor_settings::read()
{
    graphics.read_presets();

    auto& settings_ini = erhe::configuration::get_ini_file("settings.ini");
    const auto& graphics_section = settings_ini.get_section("graphics");
    graphics_section.get("preset", graphics.current_graphics_preset.name);

    const auto& imgui_section = settings_ini.get_section("imgui");
    imgui_section.get("primary_font",   imgui.primary_font);
    imgui_section.get("mono_font",      imgui.mono_font);
    imgui_section.get("font_size",      imgui.font_size);
    imgui_section.get("vr_font_size",   imgui.vr_font_size);
    imgui_section.get("icon_font_size", imgui.icon_font_size);

    const auto& icons_section = settings_ini.get_section("icons");
    icons_section.get("small_icon_size",  icon_settings.small_icon_size);
    icons_section.get("large_icon_size",  icon_settings.large_icon_size);
    icons_section.get("hotbar_icon_size", icon_settings.hotbar_icon_size);
}

void Editor_settings::write()
{
    graphics.write_presets();

    mINI::INIFile file("settings.ini");
    mINI::INIStructure ini;

    ini["graphics"]["preset"] = graphics.current_graphics_preset.name;

    ini["imgui"]["primary_font"] = imgui.primary_font;
    ini["imgui"]["mono_font"   ] = imgui.mono_font;
    ini["imgui"]["font_size"   ] = fmt::format("{}", imgui.font_size);
    ini["imgui"]["vr_font_size"] = fmt::format("{}", imgui.vr_font_size);

    ini["icons"]["small_icon_size" ] = fmt::format("{}", icon_settings.small_icon_size);
    ini["icons"]["large_icon_size" ] = fmt::format("{}", icon_settings.large_icon_size);
    ini["icons"]["hotbar_icon_size"] = fmt::format("{}", icon_settings.hotbar_icon_size);
    file.generate(ini);
}

}
