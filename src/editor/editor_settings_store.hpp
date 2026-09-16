#pragma once

#include "config/generated/editor_settings_config.hpp"
#include "config/generated/user_state_config.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace editor {

static const char* const c_editor_settings_file_path        = "config/editor/editor_settings.json";
static const char* const c_editor_settings_openxr_file_path = "config/editor/openxr_editor_settings.json";
static const char* const c_user_state_file_path             = "config/editor/user_state.json";
static const char* const c_user_state_openxr_file_path      = "config/editor/openxr_user_state.json";

// Owns Editor_settings_config, User_state_config and their persistence. Both
// are loaded at construction.
//
// The two structs are stored in separate files with the same lifecycle:
// editor_settings.json holds the tuning knobs the Settings window edits, and
// user_state.json holds the state the user builds up by using the editor -
// the inventory / hotbar slot contents and the per scene view scene / camera /
// visual style selections. A user_state.json that does not exist yet is
// seeded from the pre-v4 editor_settings.json sections the two structs were
// split out of, so an existing setup carries over.
//
// Desktop and OpenXR sessions keep separate settings files (matching the
// openxr_commands.json / openxr_ imgui-config convention): the shared
// editor_settings.json is always loaded first and its headset.openxr flag
// selects the mode; when it says OpenXR, the store switches to
// openxr_editor_settings.json for both load and save, seeding that file
// from the shared one on first OpenXR run so existing tuning carries over.
// Mode selection stays in the shared file only - the OpenXR file's own
// headset.openxr value is forced true after load so in-session state is
// consistent.
//
// Subsystems that keep their live state outside the config structs (grid,
// inventory, scene view debug visualizations, App_settings, ...) register a
// collect callback that copies that state into them. The store runs the
// callbacks, detects changes against the last saved state of each file, and
// autosaves the ones that changed. Serialization is owned here; other code only provides plain
// data copies.
class Editor_settings_store
{
public:
    Editor_settings_store();

    using Collect_callback = std::function<void(Editor_settings_config&, User_state_config&)>;

    // Registers a callback that copies live editor state into the config
    // structs. Runs before change detection and before every save. Returns
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

    // The loaded user state. Same rules as get_settings(): direct edits are
    // autosaved by update() once touch() marks the store dirty.
    [[nodiscard]] auto get_user_state()       ->       User_state_config&;
    [[nodiscard]] auto get_user_state() const -> const User_state_config&;

    // Marks the settings as possibly changed. Called from the change site
    // (typically the ImGui widget edit that mutated settings or live state);
    // spurious calls are harmless - update() still compares against the last
    // saved state before writing.
    void touch();

    // Once per frame: when touched, collect, compare each file against its
    // last saved state and autosave the ones that changed. allow_save should
    // be false while the user is interacting (e.g. mouse button held dragging
    // a slider) so a drag results in a single write when it ends. Steady-state
    // cost (not touched) is a bool test; the first call establishes the
    // baseline without writing.
    void update(bool allow_save);

    // Shutdown flush: collect, compare, and write the files that changed -
    // even without touch(), as a safety net for change sites that failed to
    // notify.
    void flush();

    // Collects and writes both files unconditionally.
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
    User_state_config           m_user_state;
    // The files this store loads from and saves to: editor_settings.json /
    // user_state.json on desktop, the openxr_ variants under OpenXR (see
    // class comment).
    std::string                 m_file_path           {c_editor_settings_file_path};
    std::string                 m_user_state_file_path{c_user_state_file_path};
    // Parts are constructed in parallel init tasks; registration must be
    // thread safe. collect() runs on the main thread per frame.
    std::mutex                  m_callbacks_mutex;
    std::vector<Callback_entry> m_collect_callbacks;
    std::size_t                 m_next_callback_id{1};
    std::string                 m_last_saved_state;
    std::string                 m_last_saved_user_state;
    bool                        m_baseline_initialized{false};
    // atomic: touch() may run from parallel init tasks (e.g. a Scene_view
    // constructor registering its first settings entry); update() runs on
    // the main thread per frame.
    std::atomic<bool>           m_dirty               {false};
};

}
