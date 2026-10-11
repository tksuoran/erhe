#include "tools/tools.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "editor_log.hpp"
#include "renderers/render_context.hpp"
#include "tools/tool.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

Tools::Tools(App_context& app_context, App_settings& /*app_settings*/)
    : m_context{app_context}
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& tool : m_tools) {
        const auto priority = tool->get_priority();
        tool->handle_priority_update(priority + 1, priority);
    }
}

void Tools::register_tool(Tool* tool)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const auto flags = tool->get_flags();
    if (erhe::utility::test_bit_set(flags, Tool_flags::background)) {
        m_background_tools.emplace_back(tool);
    } else {
        m_tools.emplace_back(tool);
    }
}

// The CPU phase of a view (no encoder): the tools record lines and labels
// into the debug and text renderers. Runs on the main thread in M2
// (Viewport_scene_view::record_view_tools); the headset view still runs it
// inside the render phase.
void Tools::render_viewport_tools(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(context.command_buffer != nullptr);
    erhe::graphics::Scoped_debug_group debug_group{*context.command_buffer, "Tools"};

    for (const auto& tool : m_background_tools) {
        tool->tool_render(context);
    }
    for (const auto& tool : m_tools) {
        tool->tool_render(context);
    }
}

void Tools::set_priority_tool(Tool* priority_tool)
{
    if (m_priority_tool == priority_tool) {
        return;
    }

    if (m_priority_tool != nullptr) {
        log_tools->trace("de-prioritizing tool {}", m_priority_tool->get_description());
        m_priority_tool->set_priority_boost(0);
    }

    m_priority_tool = priority_tool;

    if (m_priority_tool != nullptr) {
        log_tools->trace("prioritizing tool {}", m_priority_tool->get_description());
        constexpr int c_priority_tool_boost = 100;
        m_priority_tool->set_priority_boost(c_priority_tool_boost);
    } else {
        log_tools->trace("active tool reset");
    }

    {
        using namespace erhe::utility;
        const bool allow_secondary =
            (m_priority_tool != nullptr) &&
            test_bit_set(m_priority_tool->get_flags(), Tool_flags::allow_secondary);
        log_tools->trace("Update tools: allow_secondary = {}", allow_secondary);
        for (auto* tool : m_tools) {
            const auto flags = tool->get_flags();
            if (test_bit_set(flags, Tool_flags::toolbox)) {
                const bool is_priority_tool = (tool == m_priority_tool);
                const bool is_secondary     = test_bit_set(flags, Tool_flags::secondary);
                const bool enable           = is_priority_tool || (allow_secondary && is_secondary);
                tool->set_enabled(enable);
                log_tools->trace(
                    "{} {}{}{}", tool->get_description(),
                    is_priority_tool ? "priority " : "",
                    is_secondary     ? "secondary " : "",
                    enable           ? "-> enabled" : "-> disabled"
                );
            }
        }
    }

    m_context.commands->sort_bindings();
    m_context.app_message_bus->tool_select.send_message(
        Tool_select_message{}
    );
}

auto Tools::get_priority_tool() const -> Tool*
{
    return m_priority_tool;
}

auto Tools::get_tools() const -> const std::vector<Tool*>&
{
    return m_tools;
}

}  // namespace editor
