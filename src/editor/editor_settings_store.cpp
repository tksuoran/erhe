#include "editor_settings_store.hpp"

#include "config/generated/editor_settings_config.hpp"
#include "config/generated/editor_settings_config_serialization.hpp"
#include "erhe_codegen/config_io.hpp"

namespace editor {

Editor_settings_store::Editor_settings_store(Editor_settings_config& settings)
    : m_settings{settings}
{
}

void Editor_settings_store::register_collect_callback(Collect_callback callback)
{
    m_collect_callbacks.push_back(std::move(callback));
}

void Editor_settings_store::collect()
{
    for (const Collect_callback& callback : m_collect_callbacks) {
        callback(m_settings);
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
