#pragma once

#include <functional>
#include <string>
#include <vector>

struct Editor_settings_config;

namespace editor {

static const char* const c_editor_settings_file_path = "config/editor/editor_settings.json";

// Owns persistence of Editor_settings_config (config/editor/editor_settings.json).
//
// Subsystems that keep their live state outside the config struct (grid,
// inventory, debug visualizations, ...) register a collect callback that
// copies that state into the config. The store runs the callbacks, detects
// changes against the last saved state, and autosaves. Serialization is
// owned here; other code only provides plain data copies.
class Editor_settings_store
{
public:
    explicit Editor_settings_store(Editor_settings_config& settings);

    using Collect_callback = std::function<void(Editor_settings_config&)>;

    // Registers a callback that copies live editor state into the config
    // struct. Runs before change detection and before every save.
    void register_collect_callback(Collect_callback callback);

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

    Editor_settings_config&       m_settings;
    std::vector<Collect_callback> m_collect_callbacks;
    std::string                   m_last_saved_state;
    bool                          m_baseline_initialized{false};
};

}
