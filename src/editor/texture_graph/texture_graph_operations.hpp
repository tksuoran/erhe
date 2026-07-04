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
