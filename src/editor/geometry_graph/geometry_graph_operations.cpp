#include "geometry_graph/geometry_graph_operations.hpp"
#include "geometry_graph/geometry_graph.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "geometry_graph/geometry_graph_window.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"

#include <fmt/format.h>

namespace editor {

auto make_link_record(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> Geometry_graph_link_record
{
    Geometry_graph_link_record record;
    record.source_pin  = source_pin;
    record.sink_pin    = sink_pin;
    record.source_node = std::dynamic_pointer_cast<Geometry_graph_node>(source_pin->get_owner_node()->node_from_this());
    record.sink_node   = std::dynamic_pointer_cast<Geometry_graph_node>(sink_pin  ->get_owner_node()->node_from_this());
    return record;
}

Geometry_graph_node_insert_remove_operation::Geometry_graph_node_insert_remove_operation(
    Geometry_graph_window&                      window,
    const std::shared_ptr<Geometry_graph_node>& node,
    const Mode                                  mode
)
    : m_window{window}
    , m_node  {node}
    , m_mode  {mode}
{
    set_description(
        fmt::format(
            "Geometry graph {} node '{}'",
            (mode == Mode::insert) ? "add" : "remove",
            node->get_name()
        )
    );
}

void Geometry_graph_node_insert_remove_operation::execute(App_context&)
{
    if (m_mode == Mode::insert) {
        insert();
    } else {
        remove();
    }
    m_window.get_graph().evaluate_if_dirty();
}

void Geometry_graph_node_insert_remove_operation::undo(App_context&)
{
    if (m_mode == Mode::insert) {
        remove();
    } else {
        insert();
    }
    m_window.get_graph().evaluate_if_dirty();
}

void Geometry_graph_node_insert_remove_operation::insert()
{
    m_window.insert_node(m_node);
    if (m_has_position) {
        m_window.set_node_position(*m_node.get(), m_position);
    }
    for (const Geometry_graph_link_record& record : m_links) {
        m_window.connect_pins(record.source_pin, record.sink_pin);
    }
    m_links.clear();
}

void Geometry_graph_node_insert_remove_operation::remove()
{
    // Capture current links and canvas position so a later insert()
    // (undo of remove, or redo of add after undo) restores them.
    m_links.clear();
    for (erhe::graph::Pin& pin : m_node->get_input_pins()) {
        for (erhe::graph::Link* link : pin.get_links()) {
            m_links.push_back(make_link_record(link->get_source(), link->get_sink()));
        }
    }
    for (erhe::graph::Pin& pin : m_node->get_output_pins()) {
        for (erhe::graph::Link* link : pin.get_links()) {
            m_links.push_back(make_link_record(link->get_source(), link->get_sink()));
        }
    }
    m_position     = m_window.get_node_position(*m_node.get());
    m_has_position = true;
    m_window.erase_node(m_node);
}

Geometry_graph_link_insert_remove_operation::Geometry_graph_link_insert_remove_operation(
    Geometry_graph_window& window,
    erhe::graph::Pin*      source_pin,
    erhe::graph::Pin*      sink_pin,
    const Mode             mode
)
    : m_window{window}
    , m_link  {make_link_record(source_pin, sink_pin)}
    , m_mode  {mode}
{
    set_description(
        fmt::format(
            "Geometry graph {} link '{}' -> '{}'",
            (mode == Mode::insert) ? "connect" : "disconnect",
            m_link.source_node ? m_link.source_node->get_name() : "?",
            m_link.sink_node   ? m_link.sink_node  ->get_name() : "?"
        )
    );
}

void Geometry_graph_link_insert_remove_operation::execute(App_context&)
{
    if (m_mode == Mode::insert) {
        insert();
    } else {
        remove();
    }
    m_window.get_graph().evaluate_if_dirty();
}

void Geometry_graph_link_insert_remove_operation::undo(App_context&)
{
    if (m_mode == Mode::insert) {
        remove();
    } else {
        insert();
    }
    m_window.get_graph().evaluate_if_dirty();
}

void Geometry_graph_link_insert_remove_operation::insert()
{
    m_window.connect_pins(m_link.source_pin, m_link.sink_pin);
}

void Geometry_graph_link_insert_remove_operation::remove()
{
    m_window.disconnect_pins(m_link.source_pin, m_link.sink_pin);
}

}
