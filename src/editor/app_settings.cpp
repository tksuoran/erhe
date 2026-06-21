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

App_settings::App_settings()
{
    // Live state owned here persists through the store's autosave like any
    // other collect-callback client. App_settings is an app-lifetime member
    // of Editor, so the callback is never unregistered.
    m_store.register_collect_callback(
        [this](Editor_settings_config& editor_settings) {
            if (!m_openxr) {
                // editor_settings.graphics_preset_name only references the
                // desktop preset list; do not write the XR preset name to it.
                editor_settings.graphics_preset_name = graphics.current_graphics_preset.name;
            }

            editor_settings.imgui.primary_font              = imgui.primary_font;
            editor_settings.imgui.mono_font                 = imgui.mono_font;
            editor_settings.imgui.font_size                 = imgui.font_size;
            editor_settings.imgui.vr_font_size              = imgui.vr_font_size;
            editor_settings.imgui.material_design_font_size = imgui.material_design_font_size;
            editor_settings.imgui.icon_font_size            = imgui.icon_font_size;

            editor_settings.icons = icon_settings;
        }
    );
}

void App_settings::apply_limits(erhe::graphics::Device& instance, App_message_bus& app_message_bus, const float window_scale_factor)
{
    imgui.scale_factor = window_scale_factor;
    graphics.get_limits(instance, erhe::dataformat::Format::format_d32_sfloat); // TODO Do not hard code depth format
    graphics.select_active_graphics_preset(app_message_bus);
}

auto App_settings::settings_store() -> Editor_settings_store&
{
    return m_store;
}

auto App_settings::config() -> Editor_settings_config&
{
    return m_store.get_settings();
}

auto App_settings::config() const -> const Editor_settings_config&
{
    return m_store.get_settings();
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

void Graphics_settings::read_presets(const bool openxr)
{
    const char* const path = openxr
        ? c_graphics_presets_openxr_file_path
        : c_graphics_presets_file_path;
    log_startup->info("Loading graphics presets from {} (openxr = {})", path, openxr);
    Graphics_presets_config presets_config = erhe::codegen::load_config<Graphics_presets_config>(path);
    graphics_presets = std::move(presets_config.presets);
    if (graphics_presets.empty()) {
        log_startup->warn("Could not read graphics presets from {}", path);
    } else {
        log_startup->info("Loaded {} graphics preset(s):", graphics_presets.size());
        for (const Graphics_preset_entry& entry : graphics_presets) {
            log_startup->info(
                "  '{}': shadow_enable={} shadow_light_count={} shadow_resolution={}",
                entry.name, entry.shadow_enable, entry.shadow_light_count, entry.shadow_resolution
            );
        }
    }
}

void Graphics_settings::write_presets(const bool openxr)
{
    log_startup->debug("Graphics_settings::write_presets()");
    const char* const path = openxr
        ? c_graphics_presets_openxr_file_path
        : c_graphics_presets_file_path;
    Graphics_presets_config presets_config;
    presets_config.presets = graphics_presets;
    erhe::codegen::save_config(presets_config, path);
}

void Graphics_settings::apply_limits(Graphics_preset_entry& graphics_preset)
{
    graphics_preset.shadow_resolution  = std::min(graphics_preset.shadow_resolution,  max_shadow_resolution);
    graphics_preset.shadow_light_count = std::min(graphics_preset.shadow_light_count, max_depth_layers);

    graphics_preset.shadow_resolution  = std::min(graphics_preset.shadow_resolution,  8192);
    graphics_preset.shadow_light_count = std::min(graphics_preset.shadow_light_count, 32);

    // Point shadow cube array (R32F) is allocated separately and is memory-heavy
    // (a 512^2 cube is ~6 MB, x 6 faces x count). Clamp resolution to the array
    // limit and keep the cube count small so the array stays bounded.
    graphics_preset.point_shadow_resolution  = std::min(graphics_preset.point_shadow_resolution, max_shadow_resolution);
    graphics_preset.point_shadow_resolution  = std::min(graphics_preset.point_shadow_resolution, 4096);
    graphics_preset.point_shadow_light_count = std::min(graphics_preset.point_shadow_light_count, 8);
    graphics_preset.point_shadow_resolution  = std::max(graphics_preset.point_shadow_resolution, 1);
    graphics_preset.point_shadow_light_count = std::max(graphics_preset.point_shadow_light_count, 0);
}

void Graphics_settings::select_active_graphics_preset(App_message_bus& app_message_bus)
{
    log_startup->debug("Graphics_settings::select_active_graphics_preset()");

    for (auto& graphics_preset : graphics_presets) {
        apply_limits(graphics_preset);
    }

    // Override configuration
    bool matched = false;
    for (std::size_t i = 0, end = graphics_presets.size(); i < end; ++i) {
        const Graphics_preset_entry& graphics_preset = graphics_presets.at(i);
        if (graphics_preset.name == current_graphics_preset.name) {
            current_graphics_preset = graphics_preset;
            log_startup->info(
                "Using graphics preset '{}': shadow_enable={} shadow_light_count={} shadow_resolution={} msaa_sample_count={}",
                current_graphics_preset.name,
                current_graphics_preset.shadow_enable,
                current_graphics_preset.shadow_light_count,
                current_graphics_preset.shadow_resolution,
                current_graphics_preset.msaa_sample_count
            );
            // Queue instead of instant send
            app_message_bus.graphics_settings.queue_message(
                Graphics_settings_message{
                    .graphics_preset = &current_graphics_preset
                }
            );
            matched = true;
            break;
        }
    }
    if (!matched) {
        log_startup->warn(
            "Graphics preset '{}' not found in loaded preset list (active settings unchanged)",
            current_graphics_preset.name
        );
    }
}

void Graphics_settings::update(App_message_bus& app_message_bus, const bool openxr, const bool allow_save)
{
    if (graphics_presets.empty()) {
        return;
    }

    // Auto-apply: keep current_graphics_preset (what the renderer reads) in
    // sync with the active preset, matched by name. Broadcast a
    // Graphics_settings_message on change so subscribers (shadow map
    // reconfigure, MSAA, etc.) react. This replaces the former manual "Use"
    // button.
    for (Graphics_preset_entry& graphics_preset : graphics_presets) {
        if (graphics_preset.name != current_graphics_preset.name) {
            continue;
        }
        apply_limits(graphics_preset);
        if (serialize(graphics_preset) != serialize(current_graphics_preset)) {
            current_graphics_preset = graphics_preset;
            app_message_bus.graphics_settings.queue_message(
                Graphics_settings_message{
                    .graphics_preset = &current_graphics_preset
                }
            );
        }
        break;
    }

    // Auto-save: write the preset list to its file when it changes. Mirrors
    // Editor_settings_store: the first tick records the baseline (so launching
    // the editor does not rewrite the file) and writes are coalesced while a
    // mouse button is held (allow_save == false during slider drags).
    Graphics_presets_config presets_config;
    presets_config.presets = graphics_presets;
    std::string serialized = serialize(presets_config);
    if (!m_presets_baseline_initialized) {
        m_last_saved_presets           = std::move(serialized);
        m_presets_baseline_initialized = true;
    } else if (allow_save && (serialized != m_last_saved_presets)) {
        write_presets(openxr);
        m_last_saved_presets = std::move(serialized);
    }
}

void App_settings::update(App_message_bus& app_message_bus, const bool allow_save)
{
    m_store.update(allow_save);
    graphics.update(app_message_bus, m_openxr, allow_save);
}

void App_settings::read(const bool openxr)
{
    log_startup->debug("App_settings::read()");

    m_openxr = openxr;

    const Editor_settings_config& editor_settings = m_store.get_settings();

    graphics.read_presets(openxr);
    if (openxr && !graphics.graphics_presets.empty()) {
        // OpenXR mode uses the dedicated XR preset list. Override
        // editor_settings.graphics_preset_name -- which targets the desktop
        // preset list -- with the first entry from the XR file so the active
        // preset matches one we actually loaded.
        graphics.current_graphics_preset.name = graphics.graphics_presets.front().name;
    } else {
        graphics.current_graphics_preset.name = editor_settings.graphics_preset_name;
    }

    imgui.primary_font              = editor_settings.imgui.primary_font;
    imgui.mono_font                 = editor_settings.imgui.mono_font;
    imgui.font_size                 = editor_settings.imgui.font_size;
    imgui.vr_font_size              = editor_settings.imgui.vr_font_size;
    imgui.material_design_font_size = editor_settings.imgui.material_design_font_size;
    imgui.icon_font_size            = editor_settings.imgui.icon_font_size;

    icon_settings = editor_settings.icons;
}

}
