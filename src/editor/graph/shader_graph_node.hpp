#pragma once

#include "graph/payload.hpp"
#include "erhe_graph/node.hpp"

#include <imgui/imgui.h>

#include <vector>

namespace ax::NodeEditor { class EditorContext; }

namespace editor {

class Editor_context;
class Sheet;
class Shader_graph;

class Node_edge
{
public:
    static constexpr int left   = 0;
    static constexpr int right  = 1;
    static constexpr int top    = 2;
    static constexpr int bottom = 3;
    static constexpr int count  = 4;
};

auto get_node_edge_name(int direction) -> const char*;

class Shader_graph_node : public erhe::graph::Node
{
public:
    Shader_graph_node(const char* label);

    auto accumulate_input_from_links(std::size_t i) -> Payload;
    auto get_output                 (std::size_t i) const -> Payload;
    auto get_input                  (std::size_t i) const -> Payload;
    void set_input                  (std::size_t i, Payload payload);
    void set_output                 (std::size_t i, Payload payload);
    void make_input_pin             (std::size_t key, std::string_view name);
    void make_output_pin            (std::size_t key, std::string_view name);

    void node_editor(Editor_context& context, ax::NodeEditor::EditorContext& node_editor);

    virtual void evaluate(Shader_graph& graph);
    virtual void imgui   ();

protected:
    friend class Node_properties_window;

    struct Node_context
    {
        Editor_context&                context;
        ax::NodeEditor::EditorContext& node_editor;
        ImDrawList*                    draw_list{nullptr};
        float                          pin_width;
        float                          pin_label_width;
        float                          side_width;
        float                          center_width;
        ImVec2                         pin_table_size{};
        ImVec2                         node_table_size{};
        ImFont*                        icon_font{nullptr};
        int                            pin_col;
        int                            pin_label_col;
        int                            pin_edge;
        float                          edge_x;
    };
    void show_pins(Node_context& context, std::vector<erhe::graph::Pin>& pins);

    void text_unformatted_edge(int edge, const char* text);

    static constexpr std::size_t pin_key_todo = 1;

    std::vector<Payload> m_input_payloads;
    std::vector<Payload> m_output_payloads;
    int                  m_input_pin_edge {Node_edge::left};
    int                  m_output_pin_edge{Node_edge::right};
};

} // namespace editor
