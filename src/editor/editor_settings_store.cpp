#include "editor_settings_store.hpp"

#include "config/generated/editor_settings_config_serialization.hpp"
#include "erhe_codegen/config_io.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace editor {

Editor_settings_store::Editor_settings_store()
    : m_settings{erhe::codegen::load_config<Editor_settings_config>(c_editor_settings_file_path)}
{
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

void Editor_settings_store::update(const bool allow_save)
{
    collect();
    std::string serialized = serialize(m_settings, 0);
    if (!m_baseline_initialized) {
        // First evaluation after startup: take the current state as the
        // baseline so launching the editor does not rewrite the file.
        m_last_saved_state     = std::move(serialized);
        m_baseline_initialized = true;
        return;
    }
    if (allow_save && (serialized != m_last_saved_state)) {
        erhe::codegen::save_config(m_settings, c_editor_settings_file_path);
        m_last_saved_state = std::move(serialized);
    }
}

void Editor_settings_store::save()
{
    collect();
    erhe::codegen::save_config(m_settings, c_editor_settings_file_path);
    m_last_saved_state     = serialize(m_settings, 0);
    m_baseline_initialized = true;
}

}
