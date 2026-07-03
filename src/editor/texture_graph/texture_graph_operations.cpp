#include "texture_graph/texture_graph_operations.hpp"
#include "texture_graph/texture_graph.hpp"
#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/texture_graph_window.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <cfloat>
#include <cmath>

namespace editor {

auto is_valid_texture_node_position(const ImVec2& position) -> bool
{
    return
        std::isfinite(position.x) && (position.x != FLT_MAX) &&
        std::isfinite(position.y) && (position.y != FLT_MAX);
}

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
    const std::shared_ptr<Texture_graph_node>& node,
    const Mode                                 mode
)
    : m_window{window}
    , m_node  {node}
    , m_mode  {mode}
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
    m_window.insert_node(m_node);
    if (m_has_position) {
        m_window.set_node_position(*m_node.get(), m_position);
    }
    for (const Texture_graph_link_record& record : m_links) {
        m_window.connect_pins(record.source_pin, record.sink_pin);
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
    m_window.erase_node(m_node);
}

Texture_graph_replace_operation::Texture_graph_replace_operation(
    Texture_graph_window&   window,
    Texture_graph_content&& new_content,
    const char*             description
)
    : m_window     {window}
    , m_new_content{std::move(new_content)}
{
    set_description(std::string{description});
}

void Texture_graph_replace_operation::execute(App_context&)
{
    if (!m_old_captured) {
        m_old_content  = capture();
        m_old_captured = true;
    }
    apply(m_new_content);
}

void Texture_graph_replace_operation::undo(App_context&)
{
    apply(m_old_content);
}

auto Texture_graph_replace_operation::capture() -> Texture_graph_content
{
    Texture_graph_content content;
    content.nodes = m_window.get_nodes();
    for (const std::shared_ptr<Texture_graph_node>& node : content.nodes) {
        content.positions.push_back(m_window.get_node_position(*node.get()));
    }
    for (const std::unique_ptr<erhe::graph::Link>& link : m_window.get_graph().get_links()) {
        content.links.push_back(make_texture_link_record(link->get_source(), link->get_sink()));
    }
    return content;
}

void Texture_graph_replace_operation::apply(const Texture_graph_content& content)
{
    const std::vector<std::shared_ptr<Texture_graph_node>> current_nodes = m_window.get_nodes(); // copy - erase_node mutates
    for (const std::shared_ptr<Texture_graph_node>& node : current_nodes) {
        m_window.erase_node(node);
    }
    for (std::size_t i = 0, end = content.nodes.size(); i < end; ++i) {
        const std::shared_ptr<Texture_graph_node>& node = content.nodes[i];
        m_window.insert_node(node);
        if (i < content.positions.size()) {
            const ImVec2 position = content.positions[i];
            if (is_valid_texture_node_position(position)) {
                m_window.set_node_position(*node.get(), position);
            }
        }
    }
    for (const Texture_graph_link_record& record : content.links) {
        m_window.connect_pins(record.source_pin, record.sink_pin);
    }
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
    Texture_graph_window& window,
    erhe::graph::Pin*     source_pin,
    erhe::graph::Pin*     sink_pin,
    const Mode            mode
)
    : m_window{window}
    , m_link  {make_texture_link_record(source_pin, sink_pin)}
    , m_mode  {mode}
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
    m_window.connect_pins(m_link.source_pin, m_link.sink_pin);
}

void Texture_graph_link_insert_remove_operation::remove()
{
    m_window.disconnect_pins(m_link.source_pin, m_link.sink_pin);
}

} // namespace editor
