#include "app_settings.hpp"
#include "app_message_bus.hpp"
#include "editor_log.hpp"

#include "erhe_configuration/configuration.hpp"

#include <fmt/format.h>

namespace editor {

App_settings::App_settings()
{
   read();
}

void App_settings::apply_limits(erhe::graphics::Device& instance, App_message_bus& app_message_bus)
{
    graphics.get_limits(instance, erhe::dataformat::Format::format_d32_sfloat); // TODO Do not hard code depth format
    graphics.select_active_graphics_preset(app_message_bus);
}

auto App_settings::get_ui_scale() const -> float
{
    return imgui.font_size / 16.0f;
}

void Graphics_settings::get_limits(const erhe::graphics::Device& instance, erhe::dataformat::Format format)
{
    msaa_sample_count_entry_s_strings.clear();
    msaa_sample_count_entry_strings.clear();
    msaa_sample_count_entry_values.clear();
    max_shadow_resolution = 4;
    max_depth_layers = 1;
    const erhe::graphics::Format_properties format_properties = instance.get_format_properties(format);
    if (format_properties.supported == false) {
        return;
    }
    const std::size_t num_sample_counts = format_properties.texture_2d_sample_counts.size();
    msaa_sample_count_entry_s_strings.resize(num_sample_counts);
    msaa_sample_count_entry_strings  .resize(num_sample_counts);
    msaa_sample_count_entry_values   .resize(num_sample_counts);
    for (std::size_t i = 0; i < num_sample_counts; ++i) {
        const int sample_count = format_properties.texture_2d_sample_counts.at(i);
        msaa_sample_count_entry_strings  [i] = fmt::format("{}", sample_count);
        msaa_sample_count_entry_s_strings[i] = msaa_sample_count_entry_strings.at(i).c_str();
        msaa_sample_count_entry_values   [i] = sample_count;
    }
    max_shadow_resolution = std::min(
        format_properties.texture_2d_array_max_width,
        format_properties.texture_2d_array_max_height
    );
    max_depth_layers = std::min(10, format_properties.texture_2d_array_max_layers);
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
            graphics_presets.push_back(graphics_preset);
        }
    }
}

void Graphics_settings::write_presets()
{
    log_startup->debug("Graphics_settings::write_presets()");
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

void Graphics_settings::select_active_graphics_preset(App_message_bus& app_message_bus)
{
    log_startup->debug("Graphics_settings::select_active_graphics_preset()");

    for (auto& graphics_preset : graphics_presets) {
        apply_limits(graphics_preset);
    }

    // Override configuration
    for (std::size_t i = 0, end = graphics_presets.size(); i < end; ++i) {
        const auto& graphics_preset = graphics_presets.at(i);
        if (graphics_preset.name == current_graphics_preset.name) {
            current_graphics_preset = graphics_preset;
            log_startup->info("Using graphics preset {}", graphics_preset.name);
            // Queue instead of instant send
            app_message_bus.queue_message(
                App_message{
                    .update_flags    = Message_flag_bit::c_flag_bit_graphics_settings,
                    .graphics_preset = &current_graphics_preset
                }
            );
            break;
        }
    }
}

void App_settings::read()
{
    log_startup->debug("App_settings::read()");

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

void App_settings::write()
{
    log_startup->debug("App_settings::write()");

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
