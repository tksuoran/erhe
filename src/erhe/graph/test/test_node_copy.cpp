#include "test_node.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace erhe::graph::test {

// Node is Item_kind::clone_using_copy_constructor: the generic editor clone
// paths (clipboard copy, selection duplicate) run the Node copy constructor
// on any selected graph node. A copy must be an independent node: its pins
// replicate the source's pin structure (direction, key, name, slot) but are
// owned by the copy and carry no links - a copy is not connected to anything,
// and it must never alias the source's Link pointers.

TEST(NodeCopy, CopyConstructedNodeOwnsItsPinsAndHasNoLinks)
{
    Graph graph{};
    Simple_node a{"a"};
    Simple_node b{"b"};
    graph.register_node(&a);
    graph.register_node(&b);
    Link* a_to_b = graph.connect(a.output_pin(0), b.input_pin(0));
    ASSERT_NE(a_to_b, nullptr);

    Simple_node copy{a};

    // Pin structure is replicated
    ASSERT_EQ(copy.get_input_pins().size(),  a.get_input_pins().size());
    ASSERT_EQ(copy.get_output_pins().size(), a.get_output_pins().size());
    EXPECT_EQ(copy.input_pin(0)->get_key(),   a.input_pin(0)->get_key());
    EXPECT_EQ(copy.input_pin(0)->get_name(),  a.input_pin(0)->get_name());
    EXPECT_EQ(copy.input_pin(0)->get_slot(),  a.input_pin(0)->get_slot());
    EXPECT_TRUE(copy.input_pin(0)->is_sink());
    EXPECT_EQ(copy.output_pin(0)->get_key(),  a.output_pin(0)->get_key());
    EXPECT_EQ(copy.output_pin(0)->get_name(), a.output_pin(0)->get_name());
    EXPECT_TRUE(copy.output_pin(0)->is_source());

    // Pins are owned by the copy, not by the source node
    EXPECT_EQ(copy.input_pin(0)->get_owner_node(),  &copy);
    EXPECT_EQ(copy.output_pin(0)->get_owner_node(), &copy);

    // The copy is not connected to anything and does not alias source links
    EXPECT_EQ(pin_link_count(copy), 0u);
    EXPECT_FALSE(node_references_link(copy, a_to_b));

    // The copy is a distinct graph entity
    EXPECT_NE(copy.get_graph_id(), a.get_graph_id());

    // The source node's connectivity is untouched
    ASSERT_EQ(a.output_pin(0)->get_links().size(), 1u);
    EXPECT_EQ(a.output_pin(0)->get_links()[0], a_to_b);

    graph.unregister_node(&a);
    graph.unregister_node(&b);
}

TEST(NodeCopy, CloneProducesIndependentNode)
{
    Graph graph{};
    std::shared_ptr<Simple_node> a = std::make_shared<Simple_node>("a");
    std::shared_ptr<Simple_node> b = std::make_shared<Simple_node>("b");
    graph.register_node(a.get());
    graph.register_node(b.get());
    Link* a_to_b = graph.connect(a->output_pin(0), b->input_pin(0));
    ASSERT_NE(a_to_b, nullptr);

    std::shared_ptr<erhe::Item_base> clone_base = a->clone();
    ASSERT_NE(clone_base, nullptr);
    std::shared_ptr<Node> clone = std::dynamic_pointer_cast<Node>(clone_base);
    ASSERT_NE(clone, nullptr);

    ASSERT_EQ(clone->get_input_pins().size(),  1u);
    ASSERT_EQ(clone->get_output_pins().size(), 1u);
    EXPECT_EQ(clone->get_input_pins()[0].get_owner_node(),  clone.get());
    EXPECT_EQ(clone->get_output_pins()[0].get_owner_node(), clone.get());
    EXPECT_EQ(pin_link_count(*clone), 0u);
    EXPECT_NE(clone->get_graph_id(), a->get_graph_id());

    graph.unregister_node(a.get());
    graph.unregister_node(b.get());
}

TEST(NodeCopy, CopyAssignmentReplacesPinStructureWithoutLinks)
{
    Graph graph{};
    Test_node source{"source"};
    source.add_input_pin (key_alpha, "in_a");
    source.add_input_pin (key_beta,  "in_b");
    source.add_output_pin(key_alpha, "out_a");

    Simple_node peer{"peer"};
    Simple_node target{"target"};
    graph.register_node(&source);
    graph.register_node(&peer);
    Link* peer_to_source = graph.connect(peer.output_pin(0), source.input_pin(0));
    ASSERT_NE(peer_to_source, nullptr);

    const int target_graph_id = target.get_graph_id();
    Node&       target_as_node = target;
    const Node& source_as_node = source;
    target_as_node = source_as_node;

    ASSERT_EQ(target.get_input_pins().size(),  2u);
    ASSERT_EQ(target.get_output_pins().size(), 1u);
    EXPECT_EQ(target.input_pin(0)->get_key(),  key_alpha);
    EXPECT_EQ(target.input_pin(1)->get_key(),  key_beta);
    EXPECT_EQ(target.output_pin(0)->get_key(), key_alpha);
    EXPECT_EQ(target.input_pin(0)->get_owner_node(),  &target);
    EXPECT_EQ(target.input_pin(1)->get_owner_node(),  &target);
    EXPECT_EQ(target.output_pin(0)->get_owner_node(), &target);
    EXPECT_EQ(pin_link_count(target), 0u);
    EXPECT_FALSE(node_references_link(target, peer_to_source));

    // Assignment keeps the target's own graph identity
    EXPECT_EQ(target.get_graph_id(), target_graph_id);

    // Source connectivity is untouched
    ASSERT_EQ(source.input_pin(0)->get_links().size(), 1u);
    EXPECT_EQ(source.input_pin(0)->get_links()[0], peer_to_source);

    graph.unregister_node(&source);
    graph.unregister_node(&peer);
}

} // namespace erhe::graph::test
