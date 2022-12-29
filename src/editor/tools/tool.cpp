#include "tools/tool.hpp"
#include "editor_log.hpp"
#include "editor_message.hpp"
#include "erhe/toolkit/bit_helpers.hpp"

namespace editor
{

void Tool::on_message(Editor_message& message)
{
    using namespace erhe::toolkit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view))
    {
        m_hover_scene_view = message.scene_view;
    }
}

auto Tool::get_priority() const -> int
{
    return m_base_priority + m_priority_boost;
}

auto Tool::get_base_priority() const -> int
{
    return m_base_priority;
}

auto Tool::get_priority_boost() const -> int
{
    return m_priority_boost;
}

auto Tool::get_hover_scene_view() const -> Scene_view*
{
    return m_hover_scene_view;
}

auto Tool::get_flags() const -> uint64_t
{
    return m_flags;
}

auto Tool::get_icon() const -> std::optional<glm::vec2>
{
    return m_icon;
}

void Tool::set_priority_boost(const int priority_boost)
{
    log_tools->info("{} priority_boost set to {}", get_description(), priority_boost);

    const int old_priority = get_priority();
    m_priority_boost = priority_boost;
    const int new_priority = get_priority();
    handle_priority_update(old_priority, new_priority);
};

void Tool::set_base_priority(const int base_priority)
{
    m_base_priority = base_priority;
}

void Tool::set_flags(const uint64_t flags)
{
    m_flags = flags;
}

void Tool::set_icon(const glm::vec2 icon)
{
    m_icon = icon;
}

} // namespace editor
