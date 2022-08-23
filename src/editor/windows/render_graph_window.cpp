#include "windows/render_graph_window.hpp"
#include "editor_scenes.hpp"
#include "scene/scene_root.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Rendergraph_window::Rendergraph_window()
    : erhe::components::Component    {c_label}
    , erhe::application::Imgui_window{c_title, c_label}
{
}

Rendergraph_window::~Rendergraph_window() noexcept
{
}

void Rendergraph_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Rendergraph_window::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Rendergraph_window::post_initialize()
{
    m_editor_scenes = get<Editor_scenes>();
    m_render_graph  = get<erhe::application::Rendergraph>();
}

void Rendergraph_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const ImGuiTreeNodeFlags parent_flags{
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };
    //const ImGuiTreeNodeFlags leaf_flags{
    //    ImGuiTreeNodeFlags_SpanFullWidth    |
    //    ImGuiTreeNodeFlags_NoTreePushOnOpen |
    //    ImGuiTreeNodeFlags_Leaf
    //};

    if (ImGui::TreeNodeEx("Render Graph", parent_flags))
    {
        const auto& render_graph_nodes = m_render_graph->get_nodes();
        for (const auto& node : render_graph_nodes)
        {
            if (ImGui::TreeNodeEx(node->name().c_str(), parent_flags))
            {
                const auto& dependencies = node->get_dependencies();
                for (const auto& dependency : dependencies)
                {
                    ImGui::Text("%s", dependency->name().c_str());
                }
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }

    const auto& scene_roots = m_editor_scenes->get_scene_roots();
    if (ImGui::TreeNodeEx("Scenes", parent_flags))
    {
        for (const auto& scene_root : scene_roots)
        {
            ImGui::Text("%s", scene_root->name().c_str());
            //if (ImGui::TreeNodeEx(scene_root->name().c_str(), parent_flags))
            //{
            //    ImGui::TreePop();
            //}
        }
        ImGui::TreePop();
    }
#endif
}

} // namespace editor
