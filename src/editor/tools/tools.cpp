#include "tools/tools.hpp"
#include "editor_scenes.hpp"
#include "scene/scene_root.hpp"
#include "scene/material_library.hpp"
#include "tools/tool.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_window.hpp"

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
    require<Editor_scenes>();
}

void Tools::initialize_component()
{
    const auto editor_scenes = get<Editor_scenes>();
    m_scene_root = std::make_shared<Scene_root>(
        editor_scenes->get_message_bus(),
        "Tool scene"
    );
    m_scene_root->material_library()->set_visible(false);

    editor_scenes->register_scene_root(m_scene_root);
}

void Tools::post_initialize()
{
    m_imgui_windows = get<erhe::application::Imgui_windows>();
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

void Tools::register_hover_tool(Tool* tool)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_hover_tools.emplace_back(tool);
}

void Tools::on_hover(Scene_view* scene_view)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    for (const auto& tool : m_hover_tools)
    {
        tool->tool_hover(scene_view);
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

[[nodiscard]] auto Tools::get_tool_scene_root() -> std::weak_ptr<Scene_root>
{
    return m_scene_root;
}

}  // namespace erhe::application
