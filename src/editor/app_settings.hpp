#pragma once

#include "config/generated/graphics_preset_entry.hpp"
#include "config/generated/icon_settings_config.hpp"
#include "config/generated/physics_config.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_imgui/imgui_renderer.hpp"

#include <string>

struct Editor_settings_config;
namespace erhe::window { class Context_window; }

namespace editor {

static const char* const c_graphics_presets_file_path = "graphics_presets.json";

class App_message_bus;

class Graphics_settings
{
public:
    void get_limits                   (const erhe::graphics::Device& instance, erhe::dataformat::Format format);
    void read_presets                 ();
    void write_presets                ();
    void apply_limits                 (Graphics_preset_entry& graphics_preset);
    void select_active_graphics_preset(App_message_bus& app_message_bus);

    Graphics_preset_entry              current_graphics_preset;
    std::vector<Graphics_preset_entry> graphics_presets;
    std::vector<const char*>           msaa_sample_count_entry_s_strings;
    std::vector<std::string>           msaa_sample_count_entry_strings;
    std::vector<int>                   msaa_sample_count_entry_values;
    int                                max_shadow_resolution{4};
    int                                max_depth_layers{1};
};

class App_settings
{
public:
    explicit App_settings();

    void apply_limits(erhe::graphics::Device& instance, App_message_bus& message_bus, float window_scale_factor);
    void read        (const Editor_settings_config& editor_settings);
    void write       (Editor_settings_config& editor_settings);

    [[nodiscard]] auto get_ui_scale() const -> float;

    // Node tree
    bool node_tree_expand_attachments{false};
    bool node_tree_show_all          {false};

    Physics_config              physics;
    Graphics_settings           graphics;
    Icon_settings_config        icon_settings;
    erhe::imgui::Imgui_settings imgui;
};

}
