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

auto Tool::get_hover_scene_view() const -> Scene_view*
{
    return m_hover_scene_view;
}

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
