#include "app_settings.hpp"
#include "app_message_bus.hpp"
#include "editor_log.hpp"

#include "config/generated/editor_settings_config.hpp"
#include "config/generated/graphics_preset_entry.hpp"
#include "config/generated/graphics_preset_entry_serialization.hpp"
#include "config/generated/graphics_presets_config.hpp"
#include "config/generated/graphics_presets_config_serialization.hpp"

#include "erhe_codegen/config_io.hpp"

#include <fmt/format.h>

namespace editor {


App_settings::App_settings() = default;

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
    Graphics_presets_config presets_config = erhe::codegen::load_config<Graphics_presets_config>(c_graphics_presets_file_path);
    graphics_presets = std::move(presets_config.presets);
    if (graphics_presets.empty()) {
        log_startup->warn("Could not read graphics presets from {}", c_graphics_presets_file_path);
    }
}

void Graphics_settings::write_presets()
{
    log_startup->debug("Graphics_settings::write_presets()");
    Graphics_presets_config presets_config;
    presets_config.presets = graphics_presets;
    erhe::codegen::save_config(presets_config, c_graphics_presets_file_path);
}

void Graphics_settings::apply_limits(Graphics_preset_entry& graphics_preset)
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
        const Graphics_preset_entry& graphics_preset = graphics_presets.at(i);
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

void App_settings::read(const Editor_settings_config& editor_settings)
{
    log_startup->debug("App_settings::read()");

    graphics.read_presets();
    graphics.current_graphics_preset.name = editor_settings.graphics_preset_name;

    imgui.primary_font              = editor_settings.imgui.primary_font;
    imgui.mono_font                 = editor_settings.imgui.mono_font;
    imgui.font_size                 = editor_settings.imgui.font_size;
    imgui.vr_font_size              = editor_settings.imgui.vr_font_size;
    imgui.material_design_font_size = editor_settings.imgui.material_design_font_size;
    imgui.icon_font_size            = editor_settings.imgui.icon_font_size;

    icon_settings = editor_settings.icons;
}

void App_settings::write(Editor_settings_config& editor_settings)
{
    log_startup->debug("App_settings::write()");

    graphics.write_presets();
    editor_settings.graphics_preset_name = graphics.current_graphics_preset.name;

    editor_settings.imgui.primary_font              = imgui.primary_font;
    editor_settings.imgui.mono_font                 = imgui.mono_font;
    editor_settings.imgui.font_size                 = imgui.font_size;
    editor_settings.imgui.vr_font_size              = imgui.vr_font_size;
    editor_settings.imgui.material_design_font_size = imgui.material_design_font_size;
    editor_settings.imgui.icon_font_size            = imgui.icon_font_size;

    editor_settings.icons = icon_settings;
}

}
