#include "app_settings.hpp"
#include "app_message_bus.hpp"
#include "editor_log.hpp"

#include "config/generated/app_settings_config.hpp"
#include "config/generated/app_settings_config_serialization.hpp"
#include "config/generated/graphics_preset_entry.hpp"
#include "config/generated/graphics_preset_entry_serialization.hpp"
#include "config/generated/graphics_presets_config.hpp"
#include "config/generated/graphics_presets_config_serialization.hpp"

#include "erhe_codegen/config_io.hpp"

#include <fmt/format.h>

namespace editor {


App_settings::App_settings()
{
    read();
}

void App_settings::apply_limits(erhe::graphics::Device& instance, App_message_bus& app_message_bus, const float window_scale_factor)
{
    imgui.scale_factor = window_scale_factor;
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
    max_depth_layers = std::min(32, format_properties.texture_2d_array_max_layers);
}

void Graphics_settings::read_presets()
{
    graphics_presets.clear();
    Graphics_presets_config presets_config = erhe::codegen::load_config<Graphics_presets_config>(c_graphics_presets_file_path);
    for (const Graphics_preset_entry& entry : presets_config.presets) {
        Graphics_preset graphics_preset;
        graphics_preset.name               = entry.name;
        graphics_preset.msaa_sample_count  = entry.msaa_sample_count;
        graphics_preset.bindless_textures  = entry.bindless_textures;
        graphics_preset.reverse_depth      = entry.reverse_depth;
        graphics_preset.shadow_enable      = entry.shadow_enable;
        graphics_preset.shadow_resolution  = entry.shadow_resolution;
        graphics_preset.shadow_light_count = entry.shadow_light_count;
        graphics_preset.shadow_depth_bits  = entry.shadow_depth_bits;
        graphics_presets.push_back(graphics_preset);
    }
    if (graphics_presets.empty()) {
        log_startup->warn("Could not read graphics presets from {}", c_graphics_presets_file_path);
    }
}

void Graphics_settings::write_presets()
{
    log_startup->debug("Graphics_settings::write_presets()");
    Graphics_presets_config presets_config;
    for (const Graphics_preset& graphics_preset : graphics_presets) {
        Graphics_preset_entry entry;
        entry.name               = graphics_preset.name;
        entry.msaa_sample_count  = graphics_preset.msaa_sample_count;
        entry.bindless_textures  = graphics_preset.bindless_textures;
        entry.reverse_depth      = graphics_preset.reverse_depth;
        entry.shadow_enable      = graphics_preset.shadow_enable;
        entry.shadow_resolution  = graphics_preset.shadow_resolution;
        entry.shadow_light_count = graphics_preset.shadow_light_count;
        entry.shadow_depth_bits  = graphics_preset.shadow_depth_bits;
        presets_config.presets.push_back(entry);
    }
    erhe::codegen::save_config(presets_config, c_graphics_presets_file_path);
}

void Graphics_settings::apply_limits(Graphics_preset& graphics_preset)
{
    graphics_preset.shadow_resolution  = std::min(graphics_preset.shadow_resolution,  max_shadow_resolution);
    graphics_preset.shadow_light_count = std::min(graphics_preset.shadow_light_count, max_depth_layers);

    graphics_preset.shadow_resolution  = std::min(graphics_preset.shadow_resolution,  8192);
    graphics_preset.shadow_light_count = std::min(graphics_preset.shadow_light_count, 32);
}

void Graphics_settings::select_active_graphics_preset(App_message_bus& app_message_bus)
{
    log_startup->debug("Graphics_settings::select_active_graphics_preset()");

    for (auto& graphics_preset : graphics_presets) {
        apply_limits(graphics_preset);
    }

    // Override configuration
    for (std::size_t i = 0, end = graphics_presets.size(); i < end; ++i) {
        const Graphics_preset& graphics_preset = graphics_presets.at(i);
        if (graphics_preset.name == current_graphics_preset.name) {
            current_graphics_preset = graphics_preset;
            log_startup->info("Using graphics preset {}", graphics_preset.name);
            // Queue instead of instant send
            app_message_bus.graphics_settings.queue_message(
                Graphics_settings_message{
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

    App_settings_config config = erhe::codegen::load_config<App_settings_config>(c_settings_file_path);
    graphics.current_graphics_preset.name = config.graphics_preset_name;

    imgui.primary_font              = config.imgui.primary_font;
    imgui.mono_font                 = config.imgui.mono_font;
    imgui.font_size                 = config.imgui.font_size;
    imgui.vr_font_size              = config.imgui.vr_font_size;
    imgui.material_design_font_size = config.imgui.material_design_font_size;
    imgui.icon_font_size            = config.imgui.icon_font_size;

    icon_settings.small_icon_size  = config.icons.small_icon_size;
    icon_settings.large_icon_size  = config.icons.large_icon_size;
    icon_settings.hotbar_icon_size = config.icons.hotbar_icon_size;
}

void App_settings::write()
{
    log_startup->debug("App_settings::write()");

    graphics.write_presets();

    App_settings_config config;
    config.graphics_preset_name = graphics.current_graphics_preset.name;

    config.imgui.primary_font              = imgui.primary_font;
    config.imgui.mono_font                 = imgui.mono_font;
    config.imgui.font_size                 = imgui.font_size;
    config.imgui.vr_font_size              = imgui.vr_font_size;
    config.imgui.material_design_font_size = imgui.material_design_font_size;
    config.imgui.icon_font_size            = imgui.icon_font_size;

    config.icons.small_icon_size  = icon_settings.small_icon_size;
    config.icons.large_icon_size  = icon_settings.large_icon_size;
    config.icons.hotbar_icon_size = icon_settings.hotbar_icon_size;

    erhe::codegen::save_config(config, c_settings_file_path);
}

}
