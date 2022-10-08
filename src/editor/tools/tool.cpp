#include "tools/tool.hpp"
#include "editor_log.hpp"

namespace editor
{

void Tool::set_enable_state(const bool enable_state)
{
    log_tools->info("{} enable_state -> {}", description(), enable_state);

    m_enabled = enable_state;
    on_enable_state_changed();
};

auto Tool::is_enabled() const -> bool
{
    return m_enabled;
}

} // namespace editor
