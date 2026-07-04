#include "texture_graph/texture_graph_operations.hpp"
#include "texture_graph/graph_texture.hpp"
#include "texture_graph/texture_graph.hpp"
#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/texture_graph_window.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace editor {

auto make_texture_link_record(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> Texture_graph_link_record
{
    Texture_graph_link_record record;
    record.source_pin  = source_pin;
    record.sink_pin    = sink_pin;
    record.source_node = std::dynamic_pointer_cast<Texture_graph_node>(source_pin->get_owner_node()->node_from_this());
    record.sink_node   = std::dynamic_pointer_cast<Texture_graph_node>(sink_pin  ->get_owner_node()->node_from_this());
    return record;
}

Texture_graph_node_insert_remove_operation::Texture_graph_node_insert_remove_operation(
    Texture_graph_window&                      window,
    const std::shared_ptr<Graph_texture>&      graph_texture,
    const std::shared_ptr<Texture_graph_node>& node,
    const Mode                                 mode
)
    : m_window       {window}
    , m_graph_texture{graph_texture}
    , m_node         {node}
    , m_mode         {mode}
{
    set_description(
        fmt::format(
            "Texture graph {} node '{}'",
            (mode == Mode::insert) ? "add" : "remove",
            node->get_name()
        )
    );
}

void Texture_graph_node_insert_remove_operation::execute(App_context&)
{
    if (m_mode == Mode::insert) {
        insert();
    } else {
        remove();
    }
}

void Texture_graph_node_insert_remove_operation::undo(App_context&)
{
    if (m_mode == Mode::insert) {
        remove();
    } else {
        insert();
    }
}

void Texture_graph_node_insert_remove_operation::insert()
{
    m_window.insert_node(*m_graph_texture, m_node);
    if (m_has_position) {
        m_window.set_node_position(*m_node.get(), m_position);
    }
    for (const Texture_graph_link_record& record : m_links) {
        m_window.connect_pins(*m_graph_texture, record.source_pin, record.sink_pin);
    }
    m_links.clear();
}

void Texture_graph_node_insert_remove_operation::remove()
{
    // Capture current links and canvas position so a later insert()
    // (undo of remove, or redo of add after undo) restores them.
    m_links.clear();
    for (erhe::graph::Pin& pin : m_node->get_input_pins()) {
        for (erhe::graph::Link* link : pin.get_links()) {
            m_links.push_back(make_texture_link_record(link->get_source(), link->get_sink()));
        }
    }
    for (erhe::graph::Pin& pin : m_node->get_output_pins()) {
        for (erhe::graph::Link* link : pin.get_links()) {
            m_links.push_back(make_texture_link_record(link->get_source(), link->get_sink()));
        }
    }
    m_position     = m_window.get_node_position(*m_node.get());
    m_has_position = true;
    m_window.erase_node(*m_graph_texture, m_node);
}

Texture_graph_parameter_operation::Texture_graph_parameter_operation(
    Texture_graph_window&                      window,
    const std::shared_ptr<Texture_graph_node>& node,
    std::string&&                              before_parameters,
    std::string&&                              after_parameters
)
    : m_window           {window}
    , m_node             {node}
    , m_before_parameters{std::move(before_parameters)}
    , m_after_parameters {std::move(after_parameters)}
{
    set_description(fmt::format("Texture graph edit parameters of '{}'", node->get_name()));
}

void Texture_graph_parameter_operation::execute(App_context&)
{
    if (m_first_execute) {
        // The new values are already live; just record the committed state.
        m_first_execute = false;
        m_node->set_committed_parameters(m_after_parameters);
    } else {
        apply(m_after_parameters);
    }
}

void Texture_graph_parameter_operation::undo(App_context&)
{
    apply(m_before_parameters);
}

void Texture_graph_parameter_operation::apply(const std::string& parameters)
{
    // The strings are write_parameters() dumps, always valid JSON.
    const nlohmann::json in = nlohmann::json::parse(parameters);
    m_node->read_parameters(in); // marks the node dirty
    m_node->set_committed_parameters(parameters);
}

Texture_graph_link_insert_remove_operation::Texture_graph_link_insert_remove_operation(
    Texture_graph_window&                 window,
    const std::shared_ptr<Graph_texture>& graph_texture,
    erhe::graph::Pin*                     source_pin,
    erhe::graph::Pin*                     sink_pin,
    const Mode                            mode
)
    : m_window       {window}
    , m_graph_texture{graph_texture}
    , m_link         {make_texture_link_record(source_pin, sink_pin)}
    , m_mode         {mode}
{
    set_description(
        fmt::format(
            "Texture graph {} link '{}' -> '{}'",
            (mode == Mode::insert) ? "connect" : "disconnect",
            m_link.source_node ? m_link.source_node->get_name() : "?",
            m_link.sink_node   ? m_link.sink_node  ->get_name() : "?"
        )
    );
}

void Texture_graph_link_insert_remove_operation::execute(App_context&)
{
    if (m_mode == Mode::insert) {
        insert();
    } else {
        remove();
    }
}

void Texture_graph_link_insert_remove_operation::undo(App_context&)
{
    if (m_mode == Mode::insert) {
        remove();
    } else {
        insert();
    }
}

void Texture_graph_link_insert_remove_operation::insert()
{
    m_window.connect_pins(*m_graph_texture, m_link.source_pin, m_link.sink_pin);
}

void Texture_graph_link_insert_remove_operation::remove()
{
    m_window.disconnect_pins(*m_graph_texture, m_link.source_pin, m_link.sink_pin);
}

} // namespace editor
