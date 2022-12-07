#include "tools/tools.hpp"
#include "editor_message_bus.hpp"
#include "editor_scenes.hpp"
#include "scene/scene_root.hpp"
#include "scene/content_library.hpp"
#include "tools/tool.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/scene/scene.hpp"

namespace editor {

Tools::Tools()
    : erhe::components::Component{c_type_name}
{
}

Tools::~Tools() noexcept
{
}

void Tools::declare_required_components()
{
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
}

void Tools::post_initialize()
{
    m_imgui_windows = get<erhe::application::Imgui_windows>();
}

[[nodiscard]] auto Tools::get_tool_scene_root() -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

void Tools::register_tool(Tool* tool)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    m_tools.emplace_back(tool);
}

void Tools::register_background_tool(Tool* tool)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    m_background_tools.emplace_back(tool);
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

}  // namespace erhe::application
