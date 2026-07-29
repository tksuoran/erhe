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

    // Applies the active preset (matched by name): syncs it into
    // current_graphics_preset and broadcasts a Graphics_settings_message when
    // it actually changed, so subscribers (shadow map reconfigure, MSAA, ...)
    // react. Called from the change site (Settings window preset edits);
    // replaces the former per-frame auto-apply.
    void apply_active_preset          (App_message_bus& app_message_bus);

    // Marks the preset list as changed so save_presets_if_dirty() writes it.
    void mark_presets_dirty           ();

    // Per-frame: writes the preset file when marked dirty. allow_save is
    // false while a mouse button is held so a slider drag coalesces into a
    // single write. Steady-state (not dirty) cost is a bool test.
    void save_presets_if_dirty        (bool openxr, bool allow_save);

    Graphics_preset_entry              current_graphics_preset;
    std::vector<Graphics_preset_entry> graphics_presets;
    std::vector<const char*>           msaa_sample_count_entry_s_strings;
    std::vector<std::string>           msaa_sample_count_entry_strings;
    std::vector<int>                   msaa_sample_count_entry_values;
    int                                max_shadow_resolution{4};
    int                                max_depth_layers{1};

private:
    // Set by Settings window preset edits; cleared when the preset file is
    // written by save_presets_if_dirty().
    bool                               m_presets_dirty{false};
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

    // Per-frame settings tick: autosaves editor_settings.json and the graphics
    // preset file when their dirty flags are set (touch() /
    // mark_presets_dirty(), called from the edit sites). allow_save is false
    // while a mouse button is held so drags coalesce into a single write.
    // Steady-state cost is two bool tests.
    void update(bool allow_save);

    // Shutdown flush: writes both files if changed, even without a dirty
    // mark - the safety net for change sites that failed to notify.
    void flush();

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
