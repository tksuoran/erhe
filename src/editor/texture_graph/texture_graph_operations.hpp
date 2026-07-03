#pragma once

#include "operations/operation.hpp"

#include <imgui/imgui.h>

#include <memory>
#include <string>
#include <vector>

namespace erhe::graph { class Pin; }

namespace editor {

class Graph_texture;
class Texture_graph_node;
class Texture_graph_window;

// A link endpoint pair, with owning-node shared pointers keeping the
// Pin objects alive while the record sits in the undo / redo stacks.
//
// (Named distinctly from the geometry graph's identically-shaped types:
// both live in namespace editor, so a shared name / signature would be an
// ODR / duplicate-symbol clash. The texture graph does not depend on
// geometry_graph - see doc/texture-graph-plan.md decision 5.)
class Texture_graph_link_record
{
public:
    std::shared_ptr<Texture_graph_node> source_node;
    std::shared_ptr<Texture_graph_node> sink_node;
    erhe::graph::Pin*                   source_pin{nullptr};
    erhe::graph::Pin*                   sink_pin  {nullptr};
};

[[nodiscard]] auto make_texture_link_record(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> Texture_graph_link_record;

// ax::NodeEditor::EditorContext::GetNodePosition() returns
// ImVec2{FLT_MAX, FLT_MAX} for nodes it has never seen (e.g. the graph
// window has not been drawn yet). Such positions must not be saved or
// restored.
[[nodiscard]] auto is_valid_texture_node_position(const ImVec2& position) -> bool;

// Undoable add / delete of one texture graph node. Removing a node also
// removes its links; they are recorded and restored on undo, together
// with the node's position on the editor canvas.
class Texture_graph_node_insert_remove_operation : public Operation
{
public:
    enum class Mode : unsigned int {
        insert = 0,
        remove
    };

    Texture_graph_node_insert_remove_operation(
        Texture_graph_window&                      window,
        const std::shared_ptr<Graph_texture>&      graph_texture,
        const std::shared_ptr<Texture_graph_node>& node,
        Mode                                       mode
    );

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void insert();
    void remove();

    Texture_graph_window&                  m_window;
    std::shared_ptr<Graph_texture>         m_graph_texture;
    std::shared_ptr<Texture_graph_node>    m_node;
    Mode                                   m_mode;
    std::vector<Texture_graph_link_record> m_links;
    ImVec2                                 m_position    {0.0f, 0.0f};
    bool                                   m_has_position{false};
};

// Snapshot of the full graph content: nodes, links and canvas node
// positions (parallel to nodes; non-finite position = leave default).
class Texture_graph_content
{
public:
    std::vector<std::shared_ptr<Texture_graph_node>> nodes;
    std::vector<Texture_graph_link_record>           links;
    std::vector<ImVec2>                              positions;
};

// Undoable replacement of the entire graph content; used by graph load
// (new content from file) and clear (empty content). The previous
// content is captured on first execute so undo restores it exactly.
class Texture_graph_replace_operation : public Operation
{
public:
    Texture_graph_replace_operation(
        Texture_graph_window&                 window,
        const std::shared_ptr<Graph_texture>& graph_texture,
        Texture_graph_content&&               new_content,
        const char*                           description
    );

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void apply(const Texture_graph_content& content);
    [[nodiscard]] auto capture() -> Texture_graph_content;

    Texture_graph_window&          m_window;
    std::shared_ptr<Graph_texture> m_graph_texture;
    Texture_graph_content m_new_content;
    Texture_graph_content m_old_content;
    bool                  m_old_captured{false};
};

// Undoable change of one node's parameters. Holds before / after
// parameter state as write_parameters() JSON dumps, applied through
// read_parameters(). The new values are already live when the operation
// is pushed (widget edit gestures commit on completion, and
// Texture_graph_window::set_node_parameters() applies before pushing),
// so the first execute() skips the apply; redo applies normally.
class Texture_graph_parameter_operation : public Operation
{
public:
    Texture_graph_parameter_operation(
        Texture_graph_window&                      window,
        const std::shared_ptr<Texture_graph_node>& node,
        std::string&&                              before_parameters,
        std::string&&                              after_parameters
    );

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void apply(const std::string& parameters);

    Texture_graph_window&               m_window;
    std::shared_ptr<Texture_graph_node> m_node;
    std::string                         m_before_parameters;
    std::string                         m_after_parameters;
    bool                                m_first_execute{true};
};

// Undoable connect / disconnect of one texture graph link.
class Texture_graph_link_insert_remove_operation : public Operation
{
public:
    enum class Mode : unsigned int {
        insert = 0,
        remove
    };

    Texture_graph_link_insert_remove_operation(
        Texture_graph_window&                 window,
        const std::shared_ptr<Graph_texture>& graph_texture,
        erhe::graph::Pin*                     source_pin,
        erhe::graph::Pin*                     sink_pin,
        Mode                                  mode
    );

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void insert();
    void remove();

    Texture_graph_window&          m_window;
    std::shared_ptr<Graph_texture> m_graph_texture;
    Texture_graph_link_record m_link;
    Mode                      m_mode;
};

} // namespace editor
