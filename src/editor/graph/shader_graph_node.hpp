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

class Flow_direction
{
public:
    static constexpr int left_to_right = 0;
    static constexpr int right_to_left = 1;
    static constexpr int top_to_bottom = 2;
    static constexpr int bottom_to_top = 3;
    static constexpr int count         = 4;
};

auto get_flow_direction_name(int direction) -> const char*;

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
        float                          pin_width;
        float                          pin_label_width;
        float                          side_width;
        float                          center_width;
        ImVec2                         pin_table_size{};
        ImVec2                         node_table_size{};
        ImFont*                        icon_font{nullptr};
    };
    void show_input_pins (Node_context& context);
    void show_output_pins(Node_context& context);

    static constexpr std::size_t pin_key_todo = 1;

    std::vector<Payload> m_input_payloads;
    std::vector<Payload> m_output_payloads;
    //bool                 m_show_config   {false};
    int                  m_flow_direction{Flow_direction::left_to_right};
};

} // namespace editor
