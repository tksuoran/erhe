#include "editor_settings_store.hpp"
#include "editor_log.hpp"

#include "config/generated/editor_settings_config_serialization.hpp"
#include "erhe_codegen/config_io.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <filesystem>

namespace editor {

Editor_settings_store::Editor_settings_store()
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
        m_file_path = c_editor_settings_openxr_file_path;
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

void Editor_settings_store::collect()
{
    const std::lock_guard<std::mutex> lock{m_callbacks_mutex};
    for (const Callback_entry& entry : m_collect_callbacks) {
        entry.callback(m_settings);
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
        // baseline so launching the editor does not rewrite the file.
        collect();
        m_last_saved_state     = serialize(m_settings, 0);
        m_baseline_initialized = true;
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
    m_dirty = false;
}

void Editor_settings_store::save()
{
    collect();
    erhe::codegen::save_config(m_settings, m_file_path.c_str());
    m_last_saved_state     = serialize(m_settings, 0);
    m_baseline_initialized = true;
    m_dirty                = false;
}

}
