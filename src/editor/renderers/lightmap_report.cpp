#include "renderers/lightmap_report.hpp"

namespace editor {

auto Lightmap_report::c_str(const Stage stage) -> const char*
{
    switch (stage) {
        case Stage::uv_unwrap: return "UV unwrap";
        case Stage::layout:    return "Layout";
        case Stage::partition: return "Partition";
        case Stage::gbuffer:   return "G-buffer";
        case Stage::bake:      return "Bake";
        case Stage::persist:   return "Persist";
        case Stage::stream:    return "Stream";
        default:               return "?";
    }
}

void Lightmap_report::add_error(const Stage stage, std::string subject, std::string message)
{
    add(stage, std::move(subject), std::move(message), false);
}

void Lightmap_report::add_warning(const Stage stage, std::string subject, std::string message)
{
    add(stage, std::move(subject), std::move(message), true);
}

void Lightmap_report::add(const Stage stage, std::string&& subject, std::string&& message, const bool is_warning)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (m_entries.size() >= s_max_entries) {
        m_entries.erase(m_entries.begin());
        ++m_dropped;
    }
    m_entries.push_back(
        Entry{
            .stage      = stage,
            .subject    = std::move(subject),
            .message    = std::move(message),
            .is_warning = is_warning
        }
    );
}

auto Lightmap_report::snapshot() const -> std::vector<Entry>
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_entries;
}

auto Lightmap_report::empty() const -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_entries.empty();
}

void Lightmap_report::clear()
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_entries.clear();
    m_dropped = 0;
}

void Lightmap_report::clear_stage(const Stage stage)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    std::erase_if(m_entries, [stage](const Entry& entry) { return entry.stage == stage; });
}

}
