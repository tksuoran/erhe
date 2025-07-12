#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <memory>

namespace erhe::imgui { class Imgui_windows; }

namespace editor {

class App_context;
class Post_processing_node;

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
    App_context&                        m_context;
    int                                 m_selection{0};
    std::weak_ptr<Post_processing_node> m_post_processing_node{};
    bool                                m_scale_size   {true};
    float                               m_size         {0.05f};
    bool                                m_linear_filter{true};
};

}
