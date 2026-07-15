#pragma once

#include "graph/payload.hpp"
#include "graph_editor/node_edge.hpp"
#include "erhe_graph/node.hpp"

#include <imgui/imgui.h>

#include <vector>

namespace ax::NodeEditor { class EditorContext; }

namespace editor {

class App_context;
class Sheet;
class Shader_graph;

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

    void node_editor(App_context& context, ax::NodeEditor::EditorContext& node_editor);

    virtual void evaluate(Shader_graph& graph);
    virtual void imgui   ();

    // Node size: requested node extent in canvas units; <= 0 = automatic
    // (content-derived). Content is NOT scaled - a wider node stretches the
    // center column, a taller node pads below the content. Adjusted from the
    // Node Properties window "Size" row.
    [[nodiscard]] auto get_ui_width () const -> float;
    [[nodiscard]] auto get_ui_height() const -> float;
    void set_ui_size(float width, float height);

protected:
    friend class Node_properties_window;

    struct Node_context
    {
        App_context&                   context;
        ax::NodeEditor::EditorContext& node_editor;
        ImDrawList*                    draw_list      {nullptr};
        float                          pin_width;
        float                          pin_label_width;
        float                          side_width     {0.0f};
        float                          center_width;
        ImVec2                         pin_table_size {};
        ImVec2                         node_table_size{};
        ImFont*                        material_design_font{nullptr};
        int                            pin_edge       {0};
        float                          edge_x         {0.0f};
    };
    void show_pins(Node_context& context, etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& pins);

    void text_unformatted_edge(int edge, const char* text);

    // Issue #251: view scale used to author node content (table columns,
    // per-widget widths, pin sizes) in screen space at the zoomed size. Set
    // from EditorContext::GetCurrentZoom() in node_editor().
    [[nodiscard]] auto content_scale() const -> float { return m_content_scale; }

    static constexpr std::size_t pin_key_todo = 1;

    std::vector<Payload> m_input_payloads;
    std::vector<Payload> m_output_payloads;
    float                m_content_scale  {1.0f};
    float                m_ui_width       {0.0f}; // canvas units; <= 0 = automatic
    float                m_ui_height      {0.0f}; // canvas units; <= 0 = automatic
    int                  m_input_pin_edge {Node_edge::left};
    int                  m_output_pin_edge{Node_edge::right};
};

}
