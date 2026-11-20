#include "level/kit_connection_flow.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace editor {

auto c_str(const Kit_flow_tie tie) -> const char*
{
    switch (tie) {
        case Kit_flow_tie::none:              return "none";
        case Kit_flow_tie::changed_in_change: return "changed_in_change";
        case Kit_flow_tie::new_connection:    return "new_connection";
        case Kit_flow_tie::connector_order:   return "connector_order";
        default:                              return "?";
    }
}

auto c_str(const Kit_flow_skip skip) -> const char*
{
    switch (skip) {
        case Kit_flow_skip::higher_priority:     return "higher_priority";
        case Kit_flow_skip::tie_changed:         return "tie_changed";
        case Kit_flow_skip::tie_new_connection:  return "tie_new_connection";
        case Kit_flow_skip::tie_connector_order: return "tie_connector_order";
        case Kit_flow_skip::no_mapping:          return "no_mapping";
        case Kit_flow_skip::locked_layout_child: return "locked_layout_child";
        case Kit_flow_skip::author_edit:         return "author_edit";
        case Kit_flow_skip::floor:               return "floor";
        case Kit_flow_skip::shape_refuses:       return "shape_refuses";
        default:                                 return "?";
    }
}

auto c_str(const Kit_flow_response_kind kind) -> const char*
{
    switch (kind) {
        case Kit_flow_response_kind::follow:    return "follow";
        case Kit_flow_response_kind::floor_fit: return "floor_fit";
        case Kit_flow_response_kind::wall_fit:  return "wall_fit";
        default:                                return "?";
    }
}

namespace {

// Whether a beats b (C4.2, C4.3).
[[nodiscard]] auto beats(const Kit_supply_candidate& a, const Kit_supply_candidate& b) -> bool
{
    if (a.priority != b.priority) {
        return a.priority > b.priority;
    }
    if (a.supplier_changed != b.supplier_changed) {
        return a.supplier_changed;
    }
    if (a.new_connection != b.new_connection) {
        return a.new_connection;
    }
    return a.connector_index < b.connector_index;
}

} // anonymous namespace

auto get_supply_loss(const Kit_supply_candidate& loser, const Kit_supply_candidate& winner) -> Kit_flow_skip
{
    if (loser.priority != winner.priority) {
        return Kit_flow_skip::higher_priority;
    }
    if (loser.supplier_changed != winner.supplier_changed) {
        return Kit_flow_skip::tie_changed;
    }
    if (loser.new_connection != winner.new_connection) {
        return Kit_flow_skip::tie_new_connection;
    }
    return Kit_flow_skip::tie_connector_order;
}

auto choose_supply(const std::span<const Kit_supply_candidate> candidates) -> Kit_supply_choice
{
    Kit_supply_choice choice;
    for (std::size_t i = 1, end = candidates.size(); i < end; ++i) {
        if (beats(candidates[i], candidates[choice.index])) {
            choice.index = i;
        }
    }
    // The tie names the rule that beat the closest equal candidate: the latest rule in C4.3's order.
    for (std::size_t i = 0, end = candidates.size(); i < end; ++i) {
        if ((i == choice.index) || (candidates[i].priority != candidates[choice.index].priority)) {
            continue;
        }
        Kit_flow_tie tie = Kit_flow_tie::connector_order;
        switch (get_supply_loss(candidates[i], candidates[choice.index])) {
            case Kit_flow_skip::tie_changed:        tie = Kit_flow_tie::changed_in_change; break;
            case Kit_flow_skip::tie_new_connection: tie = Kit_flow_tie::new_connection;    break;
            default:                                break;
        }
        choice.tie = std::max(choice.tie, tie);
    }
    return choice;
}

auto Kit_flow_connection::operator==(const Kit_flow_connection& other_connection) const -> bool
{
    return (piece == other_connection.piece) && (connector == other_connection.connector) &&
           (other == other_connection.other) && (this->other_connector == other_connection.other_connector);
}

void Kit_flow_report::clear()
{
    taken.clear();
    not_taken.clear();
    responses.clear();
    new_connections.clear();
    removed_connections.clear();
    apart_connections.clear();
    refusal.clear();
    limit_reached = false;
    rounds        = 0;
    pending.clear();
}

auto Kit_flow_report::is_empty() const -> bool
{
    return taken.empty() && not_taken.empty() && responses.empty() && new_connections.empty() && removed_connections.empty() &&
           apart_connections.empty() && refusal.empty() && !limit_reached;
}

void Kit_flow_report::add_not_taken(Kit_flow_skipped_value&& value)
{
    for (Kit_flow_skipped_value& entry : not_taken) {
        if ((entry.piece == value.piece) && (entry.property == value.property) && (entry.supplier == value.supplier)) {
            entry = std::move(value);
            return;
        }
    }
    not_taken.push_back(std::move(value));
}

void Kit_flow_report::add_taken(Kit_flow_value&& value)
{
    // A value taken is no longer one the piece did not take from that supplier.
    std::erase_if(not_taken, [&value](const Kit_flow_skipped_value& entry) {
        return (entry.piece == value.piece) && (entry.property == value.property) && (entry.supplier == value.supplier);
    });
    for (Kit_flow_value& entry : taken) {
        if ((entry.piece == value.piece) && (entry.property == value.property)) {
            entry = std::move(value);
            return;
        }
    }
    taken.push_back(std::move(value));
}

void Kit_flow_report::add_response(Kit_flow_response&& response)
{
    for (const Kit_flow_response& entry : responses) {
        if ((entry.piece == response.piece) && (entry.kind == response.kind) && (entry.from == response.from)) {
            return;
        }
    }
    responses.push_back(std::move(response));
}

void Kit_flow_report::add_connection(std::vector<Kit_flow_connection>& list, Kit_flow_connection&& connection)
{
    if (std::find(list.begin(), list.end(), connection) == list.end()) {
        list.push_back(std::move(connection));
    }
}

auto Kit_flow_report::make_summary() const -> std::string
{
    if (!refusal.empty()) {
        return fmt::format("Flow refused: {}", refusal);
    }
    std::string summary = fmt::format(
        "Flow: {} values taken, {} not taken, {} responses, {} new, {} removed, {} apart connections",
        taken.size(), not_taken.size(), responses.size(), new_connections.size(), removed_connections.size(), apart_connections.size()
    );
    if (limit_reached) {
        summary += fmt::format("; step limit reached with {} pieces still changing", pending.size());
    }
    return summary;
}

auto Kit_follow_order::is_joined(const erhe::scene::Node* const node) const -> bool
{
    return std::find(m_joined_nodes.begin(), m_joined_nodes.end(), node) != m_joined_nodes.end();
}

void Kit_follow_order::take(const std::size_t entry, const Kit_follow_action action, const std::span<const Kit_follow_entry> entries)
{
    m_done[entry] = 1;
    m_steps.push_back(Kit_follow_step{.entry = entry, .action = action});
    if ((action == Kit_follow_action::join) && !is_joined(entries[entry].node)) {
        m_joined_nodes.push_back(entries[entry].node);
    }
}

} // namespace editor
