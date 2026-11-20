#include "level/kit_connection_flow.hpp"

#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace {

using editor::Kit_follow_action;
using editor::Kit_flow_skip;
using editor::Kit_flow_tie;
using editor::Kit_follow_entry;
using editor::Kit_follow_order;
using editor::Kit_follow_step;
using editor::Kit_supply_candidate;
using editor::Kit_supply_choice;

// A hand-built graph of attachment entries: entry i of piece nodes[i] follows piece targets[i]; pieces are numbered.
// Whether an entry is to be joined in the change, and whether its own piece changed (Kit_follow_entry).
enum class Entry_follow : unsigned int { left, affected };
enum class Entry_origin : unsigned int { reached, changed };

class Follow_graph
{
public:
    Follow_graph()
    {
        for (int i = 0; i < 8; ++i) {
            pieces.push_back(std::make_shared<erhe::scene::Xform>("piece " + std::to_string(i)));
        }
    }

    std::vector<std::shared_ptr<erhe::scene::Xform>> pieces;
    std::vector<const erhe::scene::Node*>            nodes;
    std::vector<const erhe::scene::Node*>            targets;
    std::vector<Kit_follow_entry>                    entries;

    void add(const std::size_t node, const std::size_t target, const Entry_follow follow, const Entry_origin origin)
    {
        nodes.push_back(pieces[node].get());
        targets.push_back(pieces[target].get());
        entries.push_back(
            Kit_follow_entry{.node = pieces[node].get(), .affected = (follow == Entry_follow::affected), .origin = (origin == Entry_origin::changed)}
        );
    }

    // Pending entry j blocks i when it moves i's target (no hierarchy in these graphs).
    [[nodiscard]] auto plan(Kit_follow_order& order) const -> std::vector<Kit_follow_step>
    {
        order.plan(entries, [this](const std::size_t i, const std::size_t j) -> bool { return nodes[j] == targets[i]; });
        return std::vector<Kit_follow_step>{order.get_steps().begin(), order.get_steps().end()};
    }
};

} // anonymous namespace

TEST(Kit_connection_flow, the_highest_priority_wins)
{
    // C4.2: W1 on F and joined to W2 takes F's length (2) over W2's (1), whatever else W2 has.
    const std::array<Kit_supply_candidate, 2> candidates{
        Kit_supply_candidate{.priority = 1, .supplier_changed = true,  .new_connection = true,  .connector_index = 0},
        Kit_supply_candidate{.priority = 2, .supplier_changed = false, .new_connection = false, .connector_index = 3}
    };
    const Kit_supply_choice choice = editor::choose_supply(candidates);
    EXPECT_EQ(choice.index, 1u);
    EXPECT_EQ(choice.tie, Kit_flow_tie::none);
    EXPECT_EQ(editor::get_supply_loss(candidates[0], candidates[1]), Kit_flow_skip::higher_priority);
}

TEST(Kit_connection_flow, each_tie_rule_in_order)
{
    // C4.3: the supplier changed in this change, then the new connection, then the earliest receiving connector.
    const std::array<Kit_supply_candidate, 2> changed{
        Kit_supply_candidate{.priority = 1, .supplier_changed = false, .new_connection = true,  .connector_index = 0},
        Kit_supply_candidate{.priority = 1, .supplier_changed = true,  .new_connection = false, .connector_index = 1}
    };
    EXPECT_EQ(editor::choose_supply(changed).index, 1u);
    EXPECT_EQ(editor::choose_supply(changed).tie, Kit_flow_tie::changed_in_change);
    EXPECT_EQ(editor::get_supply_loss(changed[0], changed[1]), Kit_flow_skip::tie_changed);

    const std::array<Kit_supply_candidate, 2> fresh{
        Kit_supply_candidate{.priority = 1, .supplier_changed = false, .new_connection = false, .connector_index = 0},
        Kit_supply_candidate{.priority = 1, .supplier_changed = false, .new_connection = true,  .connector_index = 1}
    };
    EXPECT_EQ(editor::choose_supply(fresh).index, 1u);
    EXPECT_EQ(editor::choose_supply(fresh).tie, Kit_flow_tie::new_connection);
    EXPECT_EQ(editor::get_supply_loss(fresh[0], fresh[1]), Kit_flow_skip::tie_new_connection);

    const std::array<Kit_supply_candidate, 3> order{
        Kit_supply_candidate{.priority = 1, .connector_index = 2},
        Kit_supply_candidate{.priority = 1, .connector_index = 0},
        Kit_supply_candidate{.priority = 0, .connector_index = 1}
    };
    EXPECT_EQ(editor::choose_supply(order).index, 1u);
    EXPECT_EQ(editor::choose_supply(order).tie, Kit_flow_tie::connector_order);
    EXPECT_EQ(editor::get_supply_loss(order[0], order[1]), Kit_flow_skip::tie_connector_order);
    EXPECT_EQ(editor::get_supply_loss(order[2], order[1]), Kit_flow_skip::higher_priority);
}

TEST(Kit_connection_flow, the_tie_names_the_closest_loser)
{
    // One loser beaten by the changed rule, one only by the connector order: the report names the connector order.
    const std::array<Kit_supply_candidate, 3> candidates{
        Kit_supply_candidate{.priority = 1, .supplier_changed = true,  .connector_index = 0},
        Kit_supply_candidate{.priority = 1, .supplier_changed = false, .connector_index = 1},
        Kit_supply_candidate{.priority = 1, .supplier_changed = true,  .connector_index = 2}
    };
    const Kit_supply_choice choice = editor::choose_supply(candidates);
    EXPECT_EQ(choice.index, 0u);
    EXPECT_EQ(choice.tie, Kit_flow_tie::connector_order);
}

TEST(Kit_connection_flow, a_chain_follows_in_dependency_order)
{
    // c follows b, b follows a (moved): b joins first, then c.
    Follow_graph graph;
    graph.add(/*c*/ 2, /*b*/ 1, Entry_follow::affected, Entry_origin::reached);
    graph.add(/*b*/ 1, /*a*/ 0, Entry_follow::affected, Entry_origin::reached);
    Kit_follow_order order;
    const std::vector<Kit_follow_step> steps = graph.plan(order);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(steps[0].entry, 1u);
    EXPECT_EQ(steps[1].entry, 0u);
    EXPECT_EQ(steps[0].action, Kit_follow_action::join);
    EXPECT_EQ(steps[1].action, Kit_follow_action::join);
}

TEST(Kit_connection_flow, a_cycle_gives_up_the_changed_piece)
{
    // a -> b -> c -> a, a's own connector moved (a changed): a keeps its pose, c joins a, then b joins c (C3.7).
    Follow_graph graph;
    graph.add(/*a*/ 0, /*b*/ 1, Entry_follow::affected, Entry_origin::changed);
    graph.add(/*b*/ 1, /*c*/ 2, Entry_follow::affected, Entry_origin::reached);
    graph.add(/*c*/ 2, /*a*/ 0, Entry_follow::affected, Entry_origin::reached);
    Kit_follow_order order;
    const std::vector<Kit_follow_step> steps = graph.plan(order);
    ASSERT_EQ(steps.size(), 3u);
    EXPECT_EQ(steps[0].entry, 0u);
    EXPECT_EQ(steps[0].action, Kit_follow_action::give_up);
    EXPECT_EQ(steps[1].entry, 2u);
    EXPECT_EQ(steps[1].action, Kit_follow_action::join);
    EXPECT_EQ(steps[2].entry, 1u);
    EXPECT_EQ(steps[2].action, Kit_follow_action::join);
}

TEST(Kit_connection_flow, a_cycle_gives_up_an_entry_of_a_joined_piece)
{
    // c follows a (moved) and b; b follows c. c joins a first; then c -> b and b -> c block each other: c's second
    // entry is given up (c joined already), and b joins c.
    Follow_graph graph;
    graph.add(/*c*/ 2, /*a*/ 0, Entry_follow::affected, Entry_origin::reached);
    graph.add(/*c*/ 2, /*b*/ 1, Entry_follow::affected, Entry_origin::reached);
    graph.add(/*b*/ 1, /*c*/ 2, Entry_follow::affected, Entry_origin::reached);
    Kit_follow_order order;
    const std::vector<Kit_follow_step> steps = graph.plan(order);
    ASSERT_EQ(steps.size(), 3u);
    EXPECT_EQ(steps[0].entry, 0u);
    EXPECT_EQ(steps[0].action, Kit_follow_action::join);
    EXPECT_EQ(steps[1].entry, 1u);
    EXPECT_EQ(steps[1].action, Kit_follow_action::give_up);
    EXPECT_EQ(steps[2].entry, 2u);
    EXPECT_EQ(steps[2].action, Kit_follow_action::join);
}

TEST(Kit_connection_flow, a_cycle_without_an_origin_joins_its_first_entry)
{
    // A cycle reached from outside with no piece joined yet: its first entry joins, which unblocks the next.
    Follow_graph graph;
    graph.add(0, 1, Entry_follow::affected, Entry_origin::reached);
    graph.add(1, 0, Entry_follow::affected, Entry_origin::reached);
    graph.add(5, 6, Entry_follow::left, Entry_origin::reached);
    Kit_follow_order order;
    const std::vector<Kit_follow_step> steps = graph.plan(order);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(steps[0].entry, 0u);
    EXPECT_EQ(steps[0].action, Kit_follow_action::join);
    EXPECT_EQ(steps[1].entry, 1u);
    EXPECT_EQ(steps[1].action, Kit_follow_action::join);
}

TEST(Kit_connection_flow, the_order_keeps_its_scratch)
{
    Follow_graph graph;
    graph.add(0, 1, Entry_follow::affected, Entry_origin::changed);
    graph.add(1, 0, Entry_follow::affected, Entry_origin::reached);
    Kit_follow_order order;
    static_cast<void>(graph.plan(order));
    const std::vector<Kit_follow_step> again = graph.plan(order);
    EXPECT_EQ(again.size(), 2u);
}

TEST(Kit_connection_flow, the_report_keeps_the_latest_reason_and_drops_taken_values)
{
    editor::Kit_flow_report report;
    EXPECT_TRUE(report.is_empty());
    const editor::Kit_flow_piece wall{.id = 7, .name = "wall"};
    const editor::Kit_flow_piece floor{.id = 3, .name = "floor"};
    const editor::Kit_flow_piece other{.id = 9, .name = "other wall"};
    report.add_not_taken(editor::Kit_flow_skipped_value{
        .piece = wall, .property = editor::Kit_standard_property::wall_thickness, .length = 0.2f, .supplier = other, .priority = 1, .reason = Kit_flow_skip::tie_changed
    });
    report.add_not_taken(editor::Kit_flow_skipped_value{
        .piece = wall, .property = editor::Kit_standard_property::wall_thickness, .length = 0.2f, .supplier = other, .priority = 1, .reason = Kit_flow_skip::higher_priority
    });
    ASSERT_EQ(report.not_taken.size(), 1u);
    EXPECT_EQ(report.not_taken[0].reason, Kit_flow_skip::higher_priority);
    report.add_not_taken(editor::Kit_flow_skipped_value{
        .piece = wall, .property = editor::Kit_standard_property::child_length, .length = 3.7f, .supplier = floor, .priority = 2, .reason = Kit_flow_skip::author_edit
    });
    report.add_taken(editor::Kit_flow_value{
        .piece = wall, .property = editor::Kit_standard_property::child_length, .length = 3.7f, .supplier = floor, .supplier_connector = "edge_0", .priority = 2
    });
    ASSERT_EQ(report.not_taken.size(), 1u);
    ASSERT_EQ(report.taken.size(), 1u);
    report.add_taken(editor::Kit_flow_value{
        .piece = wall, .property = editor::Kit_standard_property::child_length, .length = 4.7f, .supplier = floor, .supplier_connector = "edge_0", .priority = 2
    });
    ASSERT_EQ(report.taken.size(), 1u);
    EXPECT_EQ(report.taken[0].length, 4.7f);
    // A later round takes thickness from another wall: replacing the taken entry also drops that supplier's not-taken entry.
    report.add_not_taken(editor::Kit_flow_skipped_value{
        .piece = wall, .property = editor::Kit_standard_property::child_length, .length = 2.0f, .supplier = other, .priority = 2, .reason = Kit_flow_skip::tie_changed
    });
    report.add_taken(editor::Kit_flow_value{
        .piece = wall, .property = editor::Kit_standard_property::child_length, .length = 2.0f, .supplier = other, .supplier_connector = "end", .priority = 2
    });
    ASSERT_EQ(report.taken.size(), 1u);
    EXPECT_EQ(report.taken[0].supplier, other);
    EXPECT_EQ(report.not_taken.size(), 1u);
    EXPECT_EQ(report.not_taken[0].property, editor::Kit_standard_property::wall_thickness);
    report.add_connection(report.apart_connections, editor::Kit_flow_connection{.piece = wall, .connector = "start", .other = other, .other_connector = "end"});
    report.add_connection(report.apart_connections, editor::Kit_flow_connection{.piece = wall, .connector = "start", .other = other, .other_connector = "end"});
    EXPECT_EQ(report.apart_connections.size(), 1u);
    report.add_response(editor::Kit_flow_response{.piece = wall, .kind = editor::Kit_flow_response_kind::follow, .from = floor});
    report.add_response(editor::Kit_flow_response{.piece = wall, .kind = editor::Kit_flow_response_kind::follow, .from = floor});
    EXPECT_EQ(report.responses.size(), 1u);
    EXPECT_EQ(report.make_summary(), "Flow: 1 values taken, 1 not taken, 1 responses, 0 new, 0 removed, 1 apart connections");
    report.limit_reached = true;
    report.pending.push_back(wall);
    EXPECT_EQ(report.make_summary(), "Flow: 1 values taken, 1 not taken, 1 responses, 0 new, 0 removed, 1 apart connections; step limit reached with 1 pieces still changing");
    report.refusal = "the outline's edges 0 and 2 cross";
    EXPECT_EQ(report.make_summary(), "Flow refused: the outline's edges 0 and 2 cross");
    report.clear();
    EXPECT_TRUE(report.is_empty());
}
