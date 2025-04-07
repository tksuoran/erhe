#pragma once

#include "windows/property_editor.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <memory>

namespace erhe {
    class Item_base;
}
namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor {

class Editor_context;
class Shader_graph_node;

class Node_properties_window : public erhe::imgui::Imgui_window
{
public:
    Node_properties_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context
    );

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

private:
    void item_flags     (const std::shared_ptr<erhe::Item_base>& item);
    void item_properties(const std::shared_ptr<erhe::Item_base>& item);
    void node_properties(Shader_graph_node& node);

    Editor_context& m_context;
    Property_editor m_property_editor;
};

} // namespace editor
