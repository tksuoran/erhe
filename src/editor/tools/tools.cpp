#include "tools/tools.hpp"
#include "editor_message_bus.hpp"
#include "editor_scenes.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"
#include "scene/content_library.hpp"
#include "tools/tool.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"

namespace editor {

Tools::Tools()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title}
{
}

Tools::~Tools() noexcept
{
}

void Tools::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
    require<Editor_message_bus>();
    require<Editor_scenes     >();
}

void Tools::initialize_component()
{
    const auto editor_message_bus = get<Editor_message_bus>();
    const auto editor_scenes = get<Editor_scenes>();
    const auto tools_content_library = std::make_shared<Content_library>();
    tools_content_library->is_shown_in_ui = false;
    m_scene_root = std::make_shared<Scene_root>(
        *m_components,
        tools_content_library,
        "Tool scene"
    );

    // TODO Maybe this is not needed/useful?
    editor_scenes->register_scene_root(m_scene_root);

    m_scene_root->get_shared_scene()->disable_flag_bits(erhe::scene::Item_flags::show_in_ui);

    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

}

void Tools::post_initialize()
{
    m_commands      = get<erhe::application::Commands     >();
    m_configuration = get<erhe::application::Configuration>();
    m_imgui_windows = get<erhe::application::Imgui_windows>();

    m_editor_message_bus = get<Editor_message_bus>();
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
        log_tools->trace("de-prioritizing tool {}", m_priority_tool->get_description());
        m_priority_tool->set_priority_boost(0);
    }

    m_priority_tool = priority_tool;

    if (m_priority_tool != nullptr)
    {
        log_tools->trace("prioritizing tool {}", m_priority_tool->get_description());
        m_priority_tool->set_priority_boost(100);
    }
    else
    {
        log_tools->trace("active tool reset");
    }

#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_configuration->headset.openxr)
    {
        using namespace erhe::toolkit;
        const bool allow_secondary =
            (m_priority_tool != nullptr) &&
            test_all_rhs_bits_set(m_priority_tool->get_flags(), Tool_flags::allow_secondary);
        for (auto* tool : m_tools)
        {
            const auto flags = tool->get_flags();
            if (test_all_rhs_bits_set(flags, Tool_flags::toolbox))
            {
                tool->set_enabled(
                    (tool == m_priority_tool) ||
                    (
                        allow_secondary &&
                        (test_all_rhs_bits_set(flags, Tool_flags::secondary))
                    )
                );
            }
        }
    }
#endif

    m_commands->sort_bindings();
    m_editor_message_bus->send_message(
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

void Tools::imgui()
{
}

}  // namespace erhe::application
