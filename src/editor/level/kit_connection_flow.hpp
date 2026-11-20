#pragma once

#include "level/kit_standard_property.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace erhe::scene { class Xformable; using Node = Xformable; }

// The scene-free parts of the connection flow (doc/editor/level_tools.md "Property flow", "Flow report").

namespace editor {

// C4.3: what decided between candidates of equal priority.
enum class Kit_flow_tie : unsigned int
{
    none = 0,          // no candidate of the winner's priority lost
    changed_in_change, // the supplier changed in this change
    new_connection,    // the connection was made in this change
    connector_order    // the receiving connector earlier in its shape's order
};

[[nodiscard]] auto c_str(Kit_flow_tie tie) -> const char*;

// One value a piece's connection offers for one standard property (C3.4).
class Kit_supply_candidate
{
public:
    int         priority        {0};
    bool        supplier_changed{false}; // the supplier changed in this change
    bool        new_connection  {false}; // the connection was made in this change
    std::size_t connector_index {0};     // the receiving connector's index in its shape's order
};

class Kit_supply_choice
{
public:
    std::size_t  index{0};               // the winner
    Kit_flow_tie tie  {Kit_flow_tie::none};
};

// Why a supplied value was not taken (C3.13).
enum class Kit_flow_skip : unsigned int
{
    higher_priority = 0, // a candidate of a higher priority won
    tie_changed,         // equal priority: a supplier changed in this change won
    tie_new_connection,  // equal priority: the connection made in this change won
    tie_connector_order, // equal priority: the connector earlier in the receiving shape's order won
    no_mapping,          // the receiving shape maps no parameter or slot to the property
    locked_layout_child, // the receiver is a child of a locked kit layout node
    author_edit,         // the author edited the receiver in this change
    floor,               // a floor takes no values
    shape_refuses        // the receiving shape cannot take the value
};

[[nodiscard]] auto c_str(Kit_flow_skip skip) -> const char*;

// C4.2, C4.3 (doc/editor/level_tools.md "Property flow"); candidates must not be empty.
[[nodiscard]] auto choose_supply(std::span<const Kit_supply_candidate> candidates) -> Kit_supply_choice;

// Why the loser is not taken against the winner.
[[nodiscard]] auto get_supply_loss(const Kit_supply_candidate& loser, const Kit_supply_candidate& winner) -> Kit_flow_skip;

class Kit_flow_piece
{
public:
    std::size_t id{0};
    std::string name;

    [[nodiscard]] auto operator==(const Kit_flow_piece& other) const -> bool { return id == other.id; }
};

// A value that flowed into a piece.
class Kit_flow_value
{
public:
    Kit_flow_piece        piece;
    Kit_standard_property property{Kit_standard_property::room_height};
    float                 length  {0.0f};  // a length property
    std::string           material;        // a material property
    Kit_flow_piece        supplier;
    std::string           supplier_connector;
    int                   priority{0};
    Kit_flow_tie          tie     {Kit_flow_tie::none};
};

// A supplied value the piece did not take, and why.
class Kit_flow_skipped_value
{
public:
    Kit_flow_piece        piece;
    Kit_standard_property property{Kit_standard_property::room_height};
    float                 length  {0.0f};
    std::string           material;
    Kit_flow_piece        supplier;
    int                   priority{0};
    Kit_flow_skip         reason  {Kit_flow_skip::higher_priority};
};

enum class Kit_flow_response_kind : unsigned int
{
    follow = 0, // a rigid move that re-joins its connector
    floor_fit,  // a floor's edge took the moved wall's base line (C3.15)
    wall_fit    // a wall took its moved neighbour's end (C3.16)
};

[[nodiscard]] auto c_str(Kit_flow_response_kind kind) -> const char*;

class Kit_flow_response
{
public:
    Kit_flow_piece         piece;
    Kit_flow_response_kind kind{Kit_flow_response_kind::follow};
    Kit_flow_piece         from; // the neighbour it answered
};

class Kit_flow_connection
{
public:
    Kit_flow_piece piece; // the end that records it
    std::string    connector;
    Kit_flow_piece other;
    std::string    other_connector;

    [[nodiscard]] auto operator==(const Kit_flow_connection& other_connection) const -> bool;
};

// C3.13, design K7.6: what one change's flow did, filled on request (cold, allocates).
class Kit_flow_report
{
public:
    std::vector<Kit_flow_value>         taken;
    std::vector<Kit_flow_skipped_value> not_taken;
    std::vector<Kit_flow_response>      responses;
    std::vector<Kit_flow_connection>    new_connections;
    std::vector<Kit_flow_connection>    removed_connections;
    std::vector<Kit_flow_connection>    apart_connections; // kept, its connectors apart (C3.7)
    std::string                         refusal;           // why the change was refused; empty when it was not
    bool                                limit_reached{false};
    std::size_t                         rounds{0};         // the value flow's rounds, the most of the change's flows
    std::vector<Kit_flow_piece>         pending;           // the pieces still changing at the step limit

    void clear();
    [[nodiscard]] auto is_empty() const -> bool;
    // A skipped value replaces the earlier one of the same piece, property and supplier (the latest round's reason).
    void add_not_taken(Kit_flow_skipped_value&& value);
    // A taken value replaces the earlier one of the same piece and property.
    void add_taken(Kit_flow_value&& value);
    void add_response(Kit_flow_response&& response);
    void add_connection(std::vector<Kit_flow_connection>& list, Kit_flow_connection&& connection);
    // One line for a status display: counts, the refusal and the step limit.
    [[nodiscard]] auto make_summary() const -> std::string;
};

// One entry of the attachment following (doc/editor/level_tools.md "Attachments"): a piece's connection to its target.
class Kit_follow_entry
{
public:
    const erhe::scene::Node* node{nullptr}; // the attached piece (only compared): a piece's entries share it
    bool        affected{false}; // the entry is to be joined in this change
    bool        origin  {false}; // the piece itself changed in this change (its own connector moved)
};

enum class Kit_follow_action : unsigned int
{
    join = 0, // join the entry's piece to its target
    give_up   // leave it where it is: a cycle has no order there (C3.7)
};

class Kit_follow_step
{
public:
    std::size_t       entry {0};
    Kit_follow_action action{Kit_follow_action::join};
};

// The order of the following, cycles included (doc/editor/level_tools.md "Attachments"); no allocation at capacity.
// blocks(i, j): pending entry j moves entry i's target or entry i's piece.
class Kit_follow_order
{
public:
    template <typename Blocks>
    void plan(std::span<const Kit_follow_entry> entries, const Blocks& blocks);

    [[nodiscard]] auto get_steps() const -> std::span<const Kit_follow_step> { return m_steps; }

private:
    [[nodiscard]] auto is_joined(const erhe::scene::Node* node) const -> bool;
    void take(std::size_t entry, Kit_follow_action action, std::span<const Kit_follow_entry> entries);

    std::vector<Kit_follow_step> m_steps;
    std::vector<std::uint8_t>    m_done;
    std::vector<const erhe::scene::Node*> m_joined_nodes;
};

template <typename Blocks>
void Kit_follow_order::plan(const std::span<const Kit_follow_entry> entries, const Blocks& blocks)
{
    m_steps.clear();
    m_joined_nodes.clear();
    m_done.assign(entries.size(), std::uint8_t{0});
    std::size_t pending = 0;
    for (std::size_t i = 0, end = entries.size(); i < end; ++i) {
        if (entries[i].affected) {
            ++pending;
        } else {
            m_done[i] = 1;
        }
    }
    while (pending > 0) {
        bool progress = false;
        for (std::size_t i = 0, end = entries.size(); i < end; ++i) {
            if (m_done[i] != 0) {
                continue;
            }
            bool blocked = false;
            for (std::size_t j = 0; (j < end) && !blocked; ++j) {
                blocked = (j != i) && (m_done[j] == 0) && blocks(i, j);
            }
            if (blocked) {
                continue;
            }
            take(i, Kit_follow_action::join, entries);
            --pending;
            progress = true;
        }
        if (progress) {
            continue;
        }
        std::size_t chosen = entries.size();
        Kit_follow_action action = Kit_follow_action::give_up;
        for (std::size_t i = 0, end = entries.size(); (i < end) && (chosen == end); ++i) {
            if ((m_done[i] == 0) && is_joined(entries[i].node)) {
                chosen = i;
            }
        }
        for (std::size_t i = 0, end = entries.size(); (i < end) && (chosen == end); ++i) {
            if ((m_done[i] == 0) && entries[i].origin) {
                chosen = i;
            }
        }
        for (std::size_t i = 0, end = entries.size(); (i < end) && (chosen == end); ++i) {
            if (m_done[i] == 0) {
                chosen = i;
                action = Kit_follow_action::join;
            }
        }
        take(chosen, action, entries);
        --pending;
    }
}

} // namespace editor
