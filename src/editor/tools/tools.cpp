#include "tools/tools.hpp"

#include "editor_message_bus.hpp"
#include "editor_scenes.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"
#include "scene/content_library.hpp"
#include "tools/tool.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor {

Tools* g_tools{nullptr};

Tools::Tools()
    : erhe::components::Component{c_type_name}
{
}

Tools::~Tools() noexcept
{
    ERHE_VERIFY(g_tools == nullptr);
}

void Tools::deinitialize_component()
{
    ERHE_VERIFY(g_tools == this);
    m_priority_tool = nullptr;
    m_tools.clear();
    m_background_tools.clear();
    m_scene_root.reset();
    g_tools = nullptr;
}

void Tools::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
    require<Editor_message_bus>();
    require<Editor_scenes     >();
}

void Tools::initialize_component()
{
    ERHE_VERIFY(g_tools == nullptr);

    const auto tools_content_library = std::make_shared<Content_library>();
    tools_content_library->is_shown_in_ui = false;
    m_scene_root = std::make_shared<Scene_root>(
        tools_content_library,
        "Tool scene"
    );

    // TODO Maybe this is not needed/useful?
    g_editor_scenes->register_scene_root(m_scene_root);

    m_scene_root->get_shared_scene()->disable_flag_bits(erhe::scene::Item_flags::show_in_ui);

    g_tools = this;
}

void Tools::post_initialize()
{
    for (const auto& tool : m_tools)
    {
        const auto priority = tool->get_priority();
        tool->handle_priority_update(priority + 1, priority);
    }
}

[[nodiscard]] auto Tools::get_tool_scene_root() -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

void Tools::register_tool(Tool* tool)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    const auto flags = tool->get_flags();
    if (erhe::toolkit::test_all_rhs_bits_set(flags, Tool_flags::background))
    {
        m_background_tools.emplace_back(tool);
    }
    else
    {
        m_tools.emplace_back(tool);
    }
}

void Tools::render_tools(const Render_context& context)
{
    for (const auto& tool : m_background_tools)
    {
        tool->tool_render(context);
    }
    for (const auto& tool : m_tools)
    {
        tool->tool_render(context);
    }
}

void Tools::set_priority_tool(Tool* priority_tool)
{
    if (m_priority_tool == priority_tool)
    {
        return;
    }

    if (m_priority_tool != nullptr)
    {
        log_tools->info("de-prioritizing tool {}", m_priority_tool->get_description());
        m_priority_tool->set_priority_boost(0);
    }

    m_priority_tool = priority_tool;

    if (m_priority_tool != nullptr)
    {
        log_tools->info("prioritizing tool {}", m_priority_tool->get_description());
        m_priority_tool->set_priority_boost(100);
    }
    else
    {
        log_tools->info("active tool reset");
    }

#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (g_headset_view->config.openxr)
    {
        using namespace erhe::toolkit;
        const bool allow_secondary =
            (m_priority_tool != nullptr) &&
            test_all_rhs_bits_set(m_priority_tool->get_flags(), Tool_flags::allow_secondary);
        log_tools->info("Update tools: allow_secondary = {}", allow_secondary);
        for (auto* tool : m_tools)
        {
            const auto flags = tool->get_flags();
            if (test_all_rhs_bits_set(flags, Tool_flags::toolbox))
            {
                const bool is_priority_tool = (tool == m_priority_tool);
                const bool is_secondary     = test_all_rhs_bits_set(flags, Tool_flags::secondary);
                const bool enable           = is_priority_tool || (allow_secondary && is_secondary);
                tool->set_enabled(enable);
                log_tools->info(
                    "{} {}{}{}", tool->get_description(),
                    is_priority_tool ? "priority " : "",
                    is_secondary     ? "secondary " : "",
                    enable           ? "-> enabled" : "-> disabled"
                );
            }
        }
    }
#endif

    erhe::application::g_commands->sort_bindings();
    g_editor_message_bus->send_message(
        Editor_message{
            .update_flags = Message_flag_bit::c_flag_bit_tool_select
        }
    );
}

[[nodiscard]] auto Tools::get_priority_tool() const -> Tool*
{
    return m_priority_tool;
}

[[nodiscard]] auto Tools::get_tools() const -> const std::vector<Tool*>&
{
    return m_tools;
}

}  // namespace erhe::application
