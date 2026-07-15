#pragma once

#include "graph_editor/node_edge.hpp"

#include "erhe_graph/node.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <string>
#include <string_view>

struct ImDrawList;
using ImU32 = unsigned int; // matches imgui.h's typedef; redeclaration to the same type is legal

namespace ax::NodeEditor { class EditorContext; }

namespace editor {

class App_context;

// Shared, payload-agnostic base for the graph editor node classes
// (Geometry_graph_node, Texture_graph_node). It owns the ax::NodeEditor
// rendering (node_editor / show_pins), the dirty flag, the factory type name,
// and the parameter (de)serialization + undo-commit plumbing - none of which
// touch the node's payload. Payload storage, input pulling, evaluate() and the
// owning-asset back-link stay in the concrete per-editor node.
//
// Three hooks vary per editor:
//   pin_key_color              - the per-editor pin-key -> color table
//   commit_parameter_operation - build + push the concrete parameter undo op
//   after_node_content         - optional extra content after imgui() (the
//                                texture graph draws its preview thumbnail here)
class Graph_editor_node : public erhe::graph::Node
{
public:
    explicit Graph_editor_node(const char* label);

    // ax::NodeEditor rendering of the whole node (header row, pins, content,
    // and the parameter-edit gesture -> undo commit).
    void node_editor(App_context& context, ax::NodeEditor::EditorContext& node_editor);

    [[nodiscard]] auto is_dirty() const -> bool;
    // Virtual so the texture graph can also invalidate its preview cache.
    virtual void mark_dirty ();
    void clear_dirty();

    // Node content widgets. Concrete nodes override; the base is empty.
    virtual void imgui();

    // Draws the node's parameter widgets (imgui()) outside the canvas - the
    // Node Properties window - with the same edit-gesture -> undoable
    // parameter operation commit node_editor() applies on the canvas.
    // Content-scale-neutral: widgets render at scale 1 regardless of the
    // canvas zoom / node size.
    void properties_imgui(App_context& context);

    // Node size: per-node scale multiplying the node's on-canvas widths and
    // font (1 = default). Adjusted from the Node Properties window; persisted
    // in the graph JSON next to the node's parameters. Clamped to [0.25, 4].
    [[nodiscard]] auto get_ui_scale() const -> float;
    void set_ui_scale(float scale);

    // Pin layout: which node edge the input / output pins are laid out on
    // (Node_edge::left / right; the renderer implements only those two, so
    // the setters clamp anything else back to the default edge). Adjusted
    // from the Node Properties window "Inputs" / "Outputs" combos; persisted
    // in the graph JSON next to the node's parameters.
    [[nodiscard]] auto get_input_pin_edge () const -> int;
    [[nodiscard]] auto get_output_pin_edge() const -> int;
    void set_input_pin_edge (int edge);
    void set_output_pin_edge(int edge);

    // Graph serialization: the factory type name is set by the node factory and
    // recreates the node on load; parameters are the node's editable values.
    // (Named to avoid clashing with erhe::Item::get_type_name().)
    [[nodiscard]] auto get_factory_type_name() const -> const std::string&;
    void set_factory_type_name(const std::string& type_name);
    virtual void write_parameters(nlohmann::json& out) const;
    virtual void read_parameters (const nlohmann::json& in);

    // Parameter undo support. The committed state is the write_parameters() JSON
    // dump the next parameter operation uses as its "before" side; node_editor()
    // captures widget edit gestures against it (one operation per completed
    // gesture, pushed on widget deactivation).
    [[nodiscard]] auto dump_parameters() const -> std::string;
    [[nodiscard]] auto get_committed_parameters() const -> const std::string&;
    void set_committed_parameters(const std::string& parameters);

protected:
    // Issue #251: node content is authored directly in screen space at the
    // zoomed size; canvas-unit pixel metrics baked into node content must be
    // multiplied by this view scale. Set from EditorContext::GetCurrentZoom()
    // at the top of node_editor().
    [[nodiscard]] auto content_scale() const -> float { return m_content_scale; }

    void show_pins(
        ax::NodeEditor::EditorContext&                                  node_editor,
        ImDrawList&                                                     draw_list,
        const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& pins,
        float                                                           edge_x,
        bool                                                            right_edge
    );

    // Per-editor hooks.
    [[nodiscard]] virtual auto pin_key_color(std::size_t key) const -> ImU32 = 0;
    virtual void commit_parameter_operation(App_context& context, std::string&& before_parameters, std::string&& after_parameters) = 0;
    virtual void after_node_content(App_context& context);

    // Shared tail of the parameter-edit gesture: once the widget deactivates,
    // dump the parameters and push one undoable operation against the
    // committed baseline. Called by node_editor() and properties_imgui().
    void commit_parameter_edit(App_context& context);

    std::string m_type_name;
    std::string m_committed_parameters;
    float       m_content_scale{1.0f};
    float       m_ui_scale{1.0f};
    int         m_input_pin_edge {Node_edge::left};
    int         m_output_pin_edge{Node_edge::right};
    bool        m_dirty{true};
    bool        m_parameter_edit_in_progress{false};
    // Whether the pointer hovers this node on the canvas (from the node
    // editor's hover state of the previous frame; set at the top of
    // node_editor()). The geometry graph uses it to spin the hovered
    // node's mesh preview.
    bool        m_is_hovered{false};
};

} // namespace editor
