#include "editor_settings_store.hpp"
#include "ai_driver.hpp"
#include "editor_log.hpp"

#include "config/generated/editor_settings_config_serialization.hpp"
#include "config/generated/user_state_config_serialization.hpp"
#include "erhe_codegen/config_io.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <filesystem>

namespace editor {

namespace {

// One-time move of the inventory / scene view sections out of a pre-v4
// settings file: Editor_settings_config keeps those fields readable (marked
// removed in v4) purely so an existing setup carries over into the new
// user_state.json. A file already written in v4 has them at their defaults,
// which is exactly the empty user state a fresh install starts from.
#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4996)
#elif defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
[[nodiscard]] auto make_user_state_from_settings(const Editor_settings_config& settings) -> User_state_config
{
    User_state_config user_state{};
    user_state.inventory   = settings.inventory;
    user_state.scene_views = settings.scene_views;
    return user_state;
}
#if defined(_MSC_VER)
#   pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic pop
#endif

}

Editor_settings_store::Editor_settings_store()
    : m_persist_user_state{!is_ai_driver()}
{
    bool upgraded = false;
    m_settings = erhe::codegen::load_config<Editor_settings_config>(c_editor_settings_file_path, &upgraded);
    log_startup->info(
        "Editor settings loaded from {} (current schema version {}, older-version detected: {})",
        c_editor_settings_file_path, Editor_settings_config::current_version, upgraded
    );

#if defined(ERHE_OS_ANDROID) && defined(ERHE_XR_LIBRARY_OPENXR)
    // The only Android flavor that links OpenXR is `quest`, which always runs
    // an immersive session (editor.cpp force-enables headset.openxr later,
    // after this store is constructed). Force the flag here too so the FIRST
    // run on a fresh install already selects the OpenXR settings file below -
    // otherwise the whole first session runs on the shared file's desktop
    // settings (e.g. hotbar placement values that put it off screen in XR)
    // and only the autosaved openxr flag fixes the second launch.
    m_settings.headset.openxr = true;
#endif

    // Separate settings per mode (see class comment): the shared file's
    // headset.openxr selects the mode; under OpenXR, switch to the OpenXR
    // settings file, seeding it from the shared file on first OpenXR run.
    if (m_settings.headset.openxr) {
        m_file_path            = c_editor_settings_openxr_file_path;
        m_user_state_file_path = c_user_state_openxr_file_path;
        std::error_code ec{};
        if (std::filesystem::exists(std::filesystem::path{c_editor_settings_openxr_file_path}, ec)) {
            m_settings = erhe::codegen::load_config<Editor_settings_config>(c_editor_settings_openxr_file_path, &upgraded);
            // Mode selection lives in the shared file only.
            m_settings.headset.openxr = true;
            log_startup->info("OpenXR mode: editor settings loaded from {}", c_editor_settings_openxr_file_path);
        } else {
            const bool ok = erhe::codegen::save_config(m_settings, c_editor_settings_openxr_file_path);
            log_startup->info("OpenXR mode: seeded {} from {} (ok={})", c_editor_settings_openxr_file_path, c_editor_settings_file_path, ok);
        }
    }

    if (upgraded) {
        // The file (or a nested section) was written by an older schema version. Rewrite
        // it now in the current format so the on-disk file is upgraded immediately on
        // load, instead of waiting for the next settings change to trigger an autosave.
        const bool ok = erhe::codegen::save_config(m_settings, m_file_path.c_str());
        log_startup->info("Rewrote {} in current schema format (ok={})", m_file_path, ok);
    }

    if (!m_persist_user_state) {
        // AI-driven run: keep the User_state_config defaults and touch no user
        // state file (see class comment).
        log_startup->info("AI-driven run: {} is neither read nor written", m_user_state_file_path);
        return;
    }

    std::error_code user_state_ec{};
    if (std::filesystem::exists(std::filesystem::path{m_user_state_file_path}, user_state_ec)) {
        bool user_state_upgraded = false;
        m_user_state = erhe::codegen::load_config<User_state_config>(m_user_state_file_path, &user_state_upgraded);
        log_startup->info(
            "User state loaded from {} (current schema version {}, older-version detected: {})",
            m_user_state_file_path, User_state_config::current_version, user_state_upgraded
        );
        if (user_state_upgraded) {
            const bool ok = erhe::codegen::save_config(m_user_state, m_user_state_file_path.c_str());
            log_startup->info("Rewrote {} in current schema format (ok={})", m_user_state_file_path, ok);
        }
    } else {
        // No user state file yet: take the sections the settings file carried
        // before the two were split (see make_user_state_from_settings()) and
        // write them where they live now.
        m_user_state = make_user_state_from_settings(m_settings);
        const bool ok = erhe::codegen::save_config(m_user_state, m_user_state_file_path.c_str());
        log_startup->info("Seeded {} from {} (ok={})", m_user_state_file_path, m_file_path, ok);
    }
}

auto Editor_settings_store::register_collect_callback(Collect_callback callback) -> std::size_t
{
    const std::lock_guard<std::mutex> lock{m_callbacks_mutex};
    const std::size_t callback_id = m_next_callback_id++;
    m_collect_callbacks.push_back(Callback_entry{callback_id, std::move(callback)});
    return callback_id;
}

void Editor_settings_store::unregister_collect_callback(const std::size_t callback_id)
{
    const std::lock_guard<std::mutex> lock{m_callbacks_mutex};
    const auto i = std::find_if(
        m_collect_callbacks.begin(),
        m_collect_callbacks.end(),
        [callback_id](const Callback_entry& entry) {
            return entry.id == callback_id;
        }
    );
    ERHE_VERIFY(i != m_collect_callbacks.end());
    m_collect_callbacks.erase(i);
}

auto Editor_settings_store::get_settings() -> Editor_settings_config&
{
    return m_settings;
}

auto Editor_settings_store::get_settings() const -> const Editor_settings_config&
{
    return m_settings;
}

auto Editor_settings_store::get_user_state() -> User_state_config&
{
    return m_user_state;
}

auto Editor_settings_store::get_user_state() const -> const User_state_config&
{
    return m_user_state;
}

void Editor_settings_store::collect()
{
    const std::lock_guard<std::mutex> lock{m_callbacks_mutex};
    for (const Callback_entry& entry : m_collect_callbacks) {
        entry.callback(m_settings, m_user_state);
    }
}

void Editor_settings_store::touch()
{
    m_dirty = true;
}

void Editor_settings_store::update(const bool allow_save)
{
    if (!m_baseline_initialized) {
        // First evaluation after startup: take the current state as the
        // baseline so launching the editor does not rewrite the files.
        collect();
        m_last_saved_state      = serialize(m_settings,   0);
        m_last_saved_user_state = m_persist_user_state ? serialize(m_user_state, 0) : std::string{};
        m_baseline_initialized  = true;
        return;
    }
    if (!m_dirty || !allow_save) {
        return;
    }
    collect();
    std::string serialized = serialize(m_settings, 0);
    if (serialized != m_last_saved_state) {
        erhe::codegen::save_config(m_settings, m_file_path.c_str());
        m_last_saved_state = std::move(serialized);
    }
    if (m_persist_user_state) {
        std::string serialized_user_state = serialize(m_user_state, 0);
        if (serialized_user_state != m_last_saved_user_state) {
            erhe::codegen::save_config(m_user_state, m_user_state_file_path.c_str());
            m_last_saved_user_state = std::move(serialized_user_state);
        }
    }
    m_dirty = false;
}

void Editor_settings_store::flush()
{
    collect();
    std::string serialized = serialize(m_settings, 0);
    if (m_baseline_initialized && (serialized != m_last_saved_state)) {
        erhe::codegen::save_config(m_settings, m_file_path.c_str());
    }
    m_last_saved_state = std::move(serialized);
    if (m_persist_user_state) {
        std::string serialized_user_state = serialize(m_user_state, 0);
        if (m_baseline_initialized && (serialized_user_state != m_last_saved_user_state)) {
            erhe::codegen::save_config(m_user_state, m_user_state_file_path.c_str());
        }
        m_last_saved_user_state = std::move(serialized_user_state);
    }
    m_dirty = false;
}

void Editor_settings_store::save()
{
    collect();
    erhe::codegen::save_config(m_settings, m_file_path.c_str());
    m_last_saved_state = serialize(m_settings, 0);
    if (m_persist_user_state) {
        erhe::codegen::save_config(m_user_state, m_user_state_file_path.c_str());
        m_last_saved_user_state = serialize(m_user_state, 0);
    }
    m_baseline_initialized = true;
    m_dirty                = false;
}

}
