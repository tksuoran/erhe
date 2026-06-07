#pragma once

#include "config/generated/editor_settings_config.hpp"

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace editor {

static const char* const c_editor_settings_file_path = "config/editor/editor_settings.json";

// Owns Editor_settings_config and its persistence
// (config/editor/editor_settings.json). The config is loaded at
// construction.
//
// Subsystems that keep their live state outside the config struct (grid,
// inventory, scene view debug visualizations, App_settings, ...) register a
// collect callback that copies that state into the config. The store runs
// the callbacks, detects changes against the last saved state, and
// autosaves. Serialization is owned here; other code only provides plain
// data copies.
class Editor_settings_store
{
public:
    Editor_settings_store();

    using Collect_callback = std::function<void(Editor_settings_config&)>;

    // Registers a callback that copies live editor state into the config
    // struct. Runs before change detection and before every save. Returns
    // an id for unregister_collect_callback(). Callers whose lifetime ends
    // before the store's (e.g. Scene_view) must unregister; app-lifetime
    // callers may ignore the id.
    auto register_collect_callback(Collect_callback callback) -> std::size_t;

    // Removes a callback registered with register_collect_callback(). Must
    // be called before the state captured by the callback is destroyed.
    void unregister_collect_callback(std::size_t callback_id);

    // The loaded settings. Sections edited directly through the mutable
    // reference (Settings window) are autosaved by update() without a
    // collect callback.
    [[nodiscard]] auto get_settings()       ->       Editor_settings_config&;
    [[nodiscard]] auto get_settings() const -> const Editor_settings_config&;

    // Once per frame: collect, compare against the last saved state and
    // autosave when changed. allow_save should be false while the user is
    // interacting (e.g. mouse button held dragging a slider) so a drag
    // results in a single write when it ends. The first call establishes
    // the baseline without writing.
    void update(bool allow_save);

    // Collects and writes the settings file unconditionally.
    void save();

private:
    void collect();

    class Callback_entry
    {
    public:
        std::size_t      id;
        Collect_callback callback;
    };

    Editor_settings_config      m_settings;
    // Parts are constructed in parallel init tasks; registration must be
    // thread safe. collect() runs on the main thread per frame.
    std::mutex                  m_callbacks_mutex;
    std::vector<Callback_entry> m_collect_callbacks;
    std::size_t                 m_next_callback_id{1};
    std::string                 m_last_saved_state;
    bool                        m_baseline_initialized{false};
};

}
