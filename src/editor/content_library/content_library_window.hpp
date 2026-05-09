#pragma once

#include <memory>
#include <string_view>

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace editor {

class App_context;
class Content_library;
class Item_tree_window;

class Content_library_window
{
public:
    Content_library_window(
        erhe::imgui::Imgui_renderer&            imgui_renderer,
        erhe::imgui::Imgui_windows&             imgui_windows,
        App_context&                            context,
        const std::shared_ptr<Content_library>& content_library,
        std::string_view                        scene_name
    );

private:
    App_context&                      m_context;
    std::shared_ptr<Content_library>  m_content_library;
    std::shared_ptr<Item_tree_window> m_tree_window;
};

}
