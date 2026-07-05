#pragma once

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

    std::string m_type_name;
    std::string m_committed_parameters;
    float       m_content_scale{1.0f};
    bool        m_dirty{true};
    bool        m_parameter_edit_in_progress{false};
};

} // namespace editor
