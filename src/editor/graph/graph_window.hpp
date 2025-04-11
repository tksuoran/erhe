#pragma once

#include "graph/shader_graph.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graph/graph.hpp"
#include "erhe_graph/node.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include "tinyexpr/tinyexpr.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::commands {
    class Commands;
}
namespace erhe::graph {
    class Link;
    class Pin;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace ax::NodeEditor {
    class EditorContext;
}

namespace editor {

class Editor_context;
class Editor_message;
class Editor_message_bus;


class Sheet;
class Shader_graph_node;
class Node_style_editor_window;

class Graph_window : public erhe::imgui::Imgui_window
{
public:
    Graph_window(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Editor_message_bus&          editor_message_bus
    );
    ~Graph_window() noexcept override;

    // Implements Imgui_window
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

private:
    auto make_constant   () -> Shader_graph_node*;
    auto make_passthrough() -> Shader_graph_node*;
    auto make_load       () -> Shader_graph_node*;
    auto make_store      () -> Shader_graph_node*;
    auto make_add        () -> Shader_graph_node*;
    auto make_sub        () -> Shader_graph_node*;
    auto make_mul        () -> Shader_graph_node*;
    auto make_div        () -> Shader_graph_node*;

    void on_message(Editor_message& message);

    Editor_context&                                 m_context;
    Shader_graph                                    m_graph;
    std::unique_ptr<ax::NodeEditor::EditorContext>  m_node_editor;

    std::unique_ptr<Node_style_editor_window>       m_style_editor_window;
    std::vector<std::shared_ptr<Shader_graph_node>> m_nodes;

};


} // namespace editor
