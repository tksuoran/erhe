#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_imgui/imgui_renderer.hpp"

#include <string>

namespace erhe::window { class Context_window; }

namespace editor {

class App_message_bus;

class Icon_settings
{
public:
    int small_icon_size {16};
    int large_icon_size {32};
    int hotbar_icon_size{128};
};

class Physics_settings
{
public:
    // Physics
    bool static_enable {true};
    bool dynamic_enable{true};
    bool debug_draw    {false};
};

class Graphics_preset
{
public:
    std::string name;
    int         msaa_sample_count{4};
    bool        bindless_textures{false};

    bool        shadow_enable     {true};
    int         shadow_resolution {1024};
    int         shadow_light_count{4};
    int         shadow_depth_bits {16};
};

static const char* const c_settings_file_path         = "settings.toml";
static const char* const c_graphics_presets_file_path = "graphics_presets.toml";

class Graphics_settings
{
public:
    void get_limits                   (const erhe::graphics::Device& instance, erhe::dataformat::Format format);
    void read_presets                 ();
    void write_presets                ();
    void apply_limits                 (Graphics_preset& graphics_preset);
    void select_active_graphics_preset(App_message_bus& app_message_bus);

    Graphics_preset              current_graphics_preset;
    std::vector<Graphics_preset> graphics_presets;
    std::vector<const char*>     msaa_sample_count_entry_s_strings;
    std::vector<std::string>     msaa_sample_count_entry_strings;
    std::vector<int>             msaa_sample_count_entry_values;
    int                          max_shadow_resolution{4};
    int                          max_depth_layers{1};
};

class App_settings
{
public:
    explicit App_settings();

    void apply_limits(erhe::graphics::Device& instance, App_message_bus& message_bus);
    void read        ();
    void write       ();

    [[nodiscard]] auto get_ui_scale() const -> float;

    // Node tree
    bool node_tree_expand_attachments{false};
    bool node_tree_show_all          {false};

    Physics_settings            physics;
    Graphics_settings           graphics;
    Icon_settings               icon_settings;
    erhe::imgui::Imgui_settings imgui;
};

}
