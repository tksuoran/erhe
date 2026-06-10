#pragma once

#include "editor_settings_store.hpp"

#include "config/generated/graphics_preset_entry.hpp"
#include "config/generated/icon_settings_config.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_imgui/imgui_renderer.hpp"

#include <string>

namespace erhe::window { class Context_window; }

namespace editor {

static const char* const c_graphics_presets_file_path        = "config/editor/graphics_presets.json";
static const char* const c_graphics_presets_openxr_file_path = "config/editor/graphics_presets_openxr.json";

class App_message_bus;

class Graphics_settings
{
public:
    void get_limits                   (const erhe::graphics::Device& instance, erhe::dataformat::Format format);
    void read_presets                 (bool openxr);
    void write_presets                (bool openxr);
    void apply_limits                 (Graphics_preset_entry& graphics_preset);
    void select_active_graphics_preset(App_message_bus& app_message_bus);

    // Per-frame: auto-apply the active preset (sync it into
    // current_graphics_preset and broadcast a Graphics_settings_message on
    // change) and auto-save the preset file when the list changes. allow_save
    // is false while a mouse button is held so a slider drag coalesces into a
    // single write. Mirrors Editor_settings_store::update(); the first call
    // records the baseline without writing.
    void update                       (App_message_bus& app_message_bus, bool openxr, bool allow_save);

    Graphics_preset_entry              current_graphics_preset;
    std::vector<Graphics_preset_entry> graphics_presets;
    std::vector<const char*>           msaa_sample_count_entry_s_strings;
    std::vector<std::string>           msaa_sample_count_entry_strings;
    std::vector<int>                   msaa_sample_count_entry_values;
    int                                max_shadow_resolution{4};
    int                                max_depth_layers{1};

private:
    // Auto-save bookkeeping for graphics_presets.json (mirrors
    // Editor_settings_store's last-saved-state comparison).
    bool                               m_presets_baseline_initialized{false};
    std::string                        m_last_saved_presets;
};

// The editor's settings root: owns Editor_settings_store (which owns the
// loaded Editor_settings_config and its autosave) plus live runtime state
// that is not a plain copy of the config -- device-derived graphics limits,
// the graphics preset list (a separate file, graphics_presets.json), the
// erhe::imgui font settings (foreign type), and ephemeral UI state.
//
// Live state persists into editor_settings.json through a collect callback
// registered at construction; explicit save calls are not needed. The
// graphics preset list is written separately via Graphics_settings::
// write_presets() (Settings window "Save Presets").
class App_settings
{
public:
    App_settings();

    void apply_limits(erhe::graphics::Device& instance, App_message_bus& message_bus, float window_scale_factor);

    // Per-frame settings tick: autosaves editor_settings.json (via the store)
    // and auto-applies + auto-saves the graphics presets. allow_save is false
    // while a mouse button is held so drags coalesce into a single write.
    void update(App_message_bus& app_message_bus, bool allow_save);

    // Hydrates live state from the loaded config and the graphics presets
    // file. Called once at startup (after the headset.openxr override
    // logic) and from the Settings window "Load Presets" button.
    void read(bool openxr);

    [[nodiscard]] auto get_ui_scale() const -> float;

    [[nodiscard]] auto settings_store()       ->       Editor_settings_store&;
    [[nodiscard]] auto config        ()       ->       Editor_settings_config&;
    [[nodiscard]] auto config        () const -> const Editor_settings_config&;

    // Node tree (ephemeral UI state, not persisted)
    bool node_tree_expand_attachments{false};
    bool node_tree_show_all          {false};

    Graphics_settings           graphics;
    Icon_settings_config        icon_settings;
    erhe::imgui::Imgui_settings imgui;

private:
    Editor_settings_store m_store;
    // Latched in read(). When running OpenXR the active preset comes from
    // the dedicated XR preset list, so its name must not be written back to
    // config().graphics_preset_name, which references the desktop list.
    bool                  m_openxr{false};
};

}
