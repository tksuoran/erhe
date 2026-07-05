#pragma once

#include "operations/operation.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/node.hpp"
#include "erhe_graph/pin.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace editor {

class App_context;

// Payload-agnostic undo / redo operations shared by the graph editors, plus the
// link-endpoint record they capture. Parameterized on a Traits class exposing:
//   using Window; using Asset; using Node;           (the concrete editor types)
//   static constexpr const char* label;              ("Geometry graph" / ...)
// The Window must expose insert_node / erase_node / connect_pins /
// disconnect_pins / get_node_position / set_node_position taking (Asset&, ...)
// (see doc/graph-editor-shared-plan.md C5). The per-editor headers instantiate
// these under the original concrete operation names via using-aliases, so
// distinct Traits give distinct instantiations - the templates sidestep the ODR
// clash that would otherwise force the identically-shaped classes apart.

// A link endpoint pair, with owning-node shared pointers keeping the Pin objects
// alive while the record sits in the undo / redo stacks.
template <typename NodeT>
class Graph_link_record
{
public:
    std::shared_ptr<NodeT> source_node;
    std::shared_ptr<NodeT> sink_node;
    erhe::graph::Pin*      source_pin{nullptr};
    erhe::graph::Pin*      sink_pin  {nullptr};
};

template <typename NodeT>
[[nodiscard]] auto make_graph_link_record(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> Graph_link_record<NodeT>
{
    Graph_link_record<NodeT> record;
    record.source_pin  = source_pin;
    record.sink_pin    = sink_pin;
    record.source_node = std::dynamic_pointer_cast<NodeT>(source_pin->get_owner_node()->node_from_this());
    record.sink_node   = std::dynamic_pointer_cast<NodeT>(sink_pin  ->get_owner_node()->node_from_this());
    return record;
}

// Undoable add / delete of one graph node. Removing a node also removes its
// links; they are recorded and restored on undo, together with the node's
// position on the editor canvas.
template <typename Traits>
class Graph_node_insert_remove_operation : public Operation
{
public:
    using Window = typename Traits::Window;
    using Asset  = typename Traits::Asset;
    using Node   = typename Traits::Node;

    enum class Mode : unsigned int {
        insert = 0,
        remove
    };

    Graph_node_insert_remove_operation(
        Window&                            window,
        const std::shared_ptr<Asset>&      asset,
        const std::shared_ptr<Node>&       node,
        const Mode                         mode
    )
        : m_window{window}
        , m_asset {asset}
        , m_node  {node}
        , m_mode  {mode}
    {
        set_description(
            fmt::format(
                "{} {} node '{}'",
                Traits::label,
                (mode == Mode::insert) ? "add" : "remove",
                node->get_name()
            )
        );
    }

    // Implements Operation
    void execute(App_context&) override { if (m_mode == Mode::insert) { insert(); } else { remove(); } }
    void undo   (App_context&) override { if (m_mode == Mode::insert) { remove(); } else { insert(); } }

private:
    void insert()
    {
        m_window.insert_node(*m_asset, m_node);
        if (m_has_position) {
            m_window.set_node_position(*m_node.get(), m_position);
        }
        for (const Graph_link_record<Node>& record : m_links) {
            m_window.connect_pins(*m_asset, record.source_pin, record.sink_pin);
        }
        m_links.clear();
    }

    void remove()
    {
        // Capture current links and canvas position so a later insert()
        // (undo of remove, or redo of add after undo) restores them.
        m_links.clear();
        for (erhe::graph::Pin& pin : m_node->get_input_pins()) {
            for (erhe::graph::Link* link : pin.get_links()) {
                m_links.push_back(make_graph_link_record<Node>(link->get_source(), link->get_sink()));
            }
        }
        for (erhe::graph::Pin& pin : m_node->get_output_pins()) {
            for (erhe::graph::Link* link : pin.get_links()) {
                m_links.push_back(make_graph_link_record<Node>(link->get_source(), link->get_sink()));
            }
        }
        m_position     = m_window.get_node_position(*m_node.get());
        m_has_position = true;
        m_window.erase_node(*m_asset, m_node);
    }

    Window&                              m_window;
    std::shared_ptr<Asset>              m_asset;
    std::shared_ptr<Node>              m_node;
    Mode                               m_mode;
    std::vector<Graph_link_record<Node>> m_links;
    ImVec2                             m_position    {0.0f, 0.0f};
    bool                               m_has_position{false};
};

// Undoable change of one node's parameters. Holds before / after parameter
// state as write_parameters() JSON dumps, applied through read_parameters().
// The new values are already live when the operation is pushed (widget edit
// gestures commit on completion, and the window's set_node_parameters() applies
// before pushing), so the first execute() skips the apply; redo applies
// normally.
template <typename Traits>
class Graph_parameter_operation : public Operation
{
public:
    using Window = typename Traits::Window;
    using Node   = typename Traits::Node;

    Graph_parameter_operation(
        Window&                            window,
        const std::shared_ptr<Node>&       node,
        std::string&&                      before_parameters,
        std::string&&                      after_parameters
    )
        : m_window           {window}
        , m_node             {node}
        , m_before_parameters{std::move(before_parameters)}
        , m_after_parameters {std::move(after_parameters)}
    {
        static_cast<void>(m_window); // kept for API parity; the apply path needs only the node
        set_description(fmt::format("{} edit parameters of '{}'", Traits::label, node->get_name()));
    }

    // Implements Operation
    void execute(App_context&) override
    {
        if (m_first_execute) {
            // The new values are already live; just record the committed state.
            m_first_execute = false;
            m_node->set_committed_parameters(m_after_parameters);
        } else {
            apply(m_after_parameters);
        }
    }

    void undo(App_context&) override { apply(m_before_parameters); }

private:
    void apply(const std::string& parameters)
    {
        // The strings are write_parameters() dumps, always valid JSON.
        const nlohmann::json in = nlohmann::json::parse(parameters);
        m_node->read_parameters(in); // marks the node dirty
        m_node->set_committed_parameters(parameters);
    }

    Window&               m_window;
    std::shared_ptr<Node> m_node;
    std::string           m_before_parameters;
    std::string           m_after_parameters;
    bool                  m_first_execute{true};
};

// Undoable connect / disconnect of one graph link.
template <typename Traits>
class Graph_link_insert_remove_operation : public Operation
{
public:
    using Window = typename Traits::Window;
    using Asset  = typename Traits::Asset;
    using Node   = typename Traits::Node;

    enum class Mode : unsigned int {
        insert = 0,
        remove
    };

    Graph_link_insert_remove_operation(
        Window&                            window,
        const std::shared_ptr<Asset>&      asset,
        erhe::graph::Pin*                  source_pin,
        erhe::graph::Pin*                  sink_pin,
        const Mode                         mode
    )
        : m_window{window}
        , m_asset {asset}
        , m_link  {make_graph_link_record<Node>(source_pin, sink_pin)}
        , m_mode  {mode}
    {
        set_description(
            fmt::format(
                "{} {} link '{}' -> '{}'",
                Traits::label,
                (mode == Mode::insert) ? "connect" : "disconnect",
                m_link.source_node ? m_link.source_node->get_name() : "?",
                m_link.sink_node   ? m_link.sink_node  ->get_name() : "?"
            )
        );
    }

    // Implements Operation
    void execute(App_context&) override { if (m_mode == Mode::insert) { insert(); } else { remove(); } }
    void undo   (App_context&) override { if (m_mode == Mode::insert) { remove(); } else { insert(); } }

private:
    void insert() { m_window.connect_pins   (*m_asset, m_link.source_pin, m_link.sink_pin); }
    void remove() { m_window.disconnect_pins(*m_asset, m_link.source_pin, m_link.sink_pin); }

    Window&                 m_window;
    std::shared_ptr<Asset>  m_asset;
    Graph_link_record<Node> m_link;
    Mode                    m_mode;
};

} // namespace editor
