#pragma once

#include "rendergraph/post_processing.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <memory>
#include <vector>

namespace erhe::imgui { class Imgui_windows; }

namespace editor {

class App_context;

// ImGui window for showing downsample steps for a Post_processing node
class Post_processing_window : public erhe::imgui::Imgui_window
{
public:
    Post_processing_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );

    // Implements Imgui_window
    void imgui() override;

private:
    void update_texture_references(const std::shared_ptr<Post_processing_node>& node);

    App_context&                        m_context;
    int                                 m_selection{0};
    std::weak_ptr<Post_processing_node> m_post_processing_node{};
    bool                                m_scale_size   {true};
    float                               m_size         {0.05f};
    bool                                m_linear_filter{true};

    Post_processing_pyramid_info                                         m_pyramid_info;
    std::weak_ptr<Post_processing_node>                                  m_references_node{};
    std::shared_ptr<Post_processing_node_texture_reference>              m_input_reference{};
    std::vector<std::shared_ptr<Post_processing_node_texture_reference>> m_downsample_references;
    std::vector<std::shared_ptr<Post_processing_node_texture_reference>> m_upsample_references;
};

}
