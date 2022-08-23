#include "windows/post_processing_window.hpp"
#include "rendergraph/post_processing.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Post_processing_window::Post_processing_window()
    : Component   {c_type_name}
    , Imgui_window{c_title, c_type_name}
{
}

void Post_processing_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Post_processing_window::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Post_processing_window::post_initialize()
{
    m_post_processing = get<Post_processing>();
}

void Post_processing_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    //ImGui::DragInt("Taps",   &m_taps,   1.0f, 1, 32);
    //ImGui::DragInt("Expand", &m_expand, 1.0f, 0, 32);
    //ImGui::DragInt("Reduce", &m_reduce, 1.0f, 0, 32);
    //ImGui::Checkbox("Linear", &m_linear);

    //const auto discrete = kernel_binom(m_taps, m_expand, m_reduce);
    //if (ImGui::TreeNodeEx("Discrete", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
    //{
    //    for (size_t i = 0; i < discrete.weights.size(); ++i)
    //    {
    //        ImGui::Text(
    //            "W: %.3f O: %.3f",
    //            discrete.weights.at(i),
    //            discrete.offsets.at(i)
    //        );
    //    }
    //    ImGui::TreePop();
    //}
    //if (m_linear)
    //{
    //    const auto linear = kernel_binom_linear(discrete);
    //    if (ImGui::TreeNodeEx("Linear", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
    //    {
    //        for (size_t i = 0; i < linear.weights.size(); ++i)
    //        {
    //            ImGui::Text(
    //                "W: %.3f O: %.3f",
    //                linear.weights.at(i),
    //                linear.offsets.at(i)
    //            );
    //        }
    //        ImGui::TreePop();
    //    }
    //}
    const auto& nodes = m_post_processing->get_nodes();
    if (nodes.empty())
    {
        return;
    }
    //// const auto post_processing = nodes.front();
    //// const auto downsample_nodes =
    ////
    //// ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
    //// for (auto& node : m_nodes)
    //// {
    ////     if (
    ////         !node.texture                ||
    ////         (node.texture->width () < 1) ||
    ////         (node.texture->height() < 1)
    ////     )
    ////     {
    ////         continue;
    ////     }
    ////
    ////     image(
    ////         node.texture,
    ////         node.texture->width (),
    ////         node.texture->height()
    ////     );
    //// }
    //// ImGui::PopStyleVar();
#endif
}

} // namespace editor
