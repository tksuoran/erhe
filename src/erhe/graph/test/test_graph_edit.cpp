#include "test_node.hpp"

#include <gtest/gtest.h>

namespace erhe::graph::test {

TEST(GraphEdit, DisconnectRemovesExactlyThatLink)
{
    Graph graph{};
    Simple_node a{"a"};
    Simple_node b{"b"};
    Simple_node c{"c"};
    graph.register_node(&a);
    graph.register_node(&b);
    graph.register_node(&c);

    Link* a_to_b = graph.connect(a.output_pin(0), b.input_pin(0));
    Link* a_to_c = graph.connect(a.output_pin(0), c.input_pin(0));
    ASSERT_NE(a_to_b, nullptr);
    ASSERT_NE(a_to_c, nullptr);
    ASSERT_EQ(graph.get_links().size(), 2u);

    graph.disconnect(a_to_b);

    // The disconnected link is gone from the graph and from both endpoint
    // pins; the sibling link is untouched.
    ASSERT_EQ(graph.get_links().size(), 1u);
    EXPECT_EQ(graph.get_links()[0].get(), a_to_c);
    ASSERT_EQ(a.output_pin(0)->get_links().size(), 1u);
    EXPECT_EQ(a.output_pin(0)->get_links()[0], a_to_c);
    EXPECT_TRUE(b.input_pin(0)->get_links().empty());
    ASSERT_EQ(c.input_pin(0)->get_links().size(), 1u);
    EXPECT_EQ(c.input_pin(0)->get_links()[0], a_to_c);

    graph.unregister_node(&a);
    graph.unregister_node(&b);
    graph.unregister_node(&c);
}

TEST(GraphEdit, UnregisterNodeLeavesNoDanglingLinksOnPeers)
{
    Graph graph{};
    Simple_node a{"a"};
    Simple_node b{"b"};
    Simple_node c{"c"};
    graph.register_node(&a);
    graph.register_node(&b);
    graph.register_node(&c);

    // a -> b -> c and a direct a -> c: removing b must drop the two links
    // touching b (a->b, b->c) and keep only a->c.
    Link* a_to_b = graph.connect(a.output_pin(0), b.input_pin(0));
    Link* b_to_c = graph.connect(b.output_pin(0), c.input_pin(0));
    Link* a_to_c = graph.connect(a.output_pin(0), c.input_pin(0));
    ASSERT_NE(a_to_b, nullptr);
    ASSERT_NE(b_to_c, nullptr);
    ASSERT_NE(a_to_c, nullptr);

    graph.unregister_node(&b);

    ASSERT_EQ(graph.get_nodes().size(), 2u);
    ASSERT_EQ(graph.get_links().size(), 1u);
    EXPECT_EQ(graph.get_links()[0].get(), a_to_c);

    // Peer nodes hold no dangling pointers to the removed links
    EXPECT_FALSE(node_references_link(a, a_to_b));
    EXPECT_FALSE(node_references_link(c, b_to_c));
    EXPECT_EQ(pin_link_count(a), 1u);
    EXPECT_EQ(pin_link_count(c), 1u);
    EXPECT_EQ(pin_link_count(b), 0u);

    graph.unregister_node(&a);
    graph.unregister_node(&c);
}

TEST(GraphEdit, UnregisterNodeWithFanOutAndFanIn)
{
    Graph graph{};
    Simple_node hub{"hub"};
    Simple_node up_0{"up_0"};
    Simple_node up_1{"up_1"};
    Simple_node down_0{"down_0"};
    Simple_node down_1{"down_1"};
    graph.register_node(&hub);
    graph.register_node(&up_0);
    graph.register_node(&up_1);
    graph.register_node(&down_0);
    graph.register_node(&down_1);

    // Two links into hub's single input pin, two links out of its single
    // output pin: unregistering must clear all four from every peer.
    ASSERT_NE(graph.connect(up_0.output_pin(0), hub.input_pin(0)), nullptr);
    ASSERT_NE(graph.connect(up_1.output_pin(0), hub.input_pin(0)), nullptr);
    ASSERT_NE(graph.connect(hub.output_pin(0), down_0.input_pin(0)), nullptr);
    ASSERT_NE(graph.connect(hub.output_pin(0), down_1.input_pin(0)), nullptr);
    ASSERT_EQ(graph.get_links().size(), 4u);

    graph.unregister_node(&hub);

    EXPECT_EQ(graph.get_nodes().size(), 4u);
    EXPECT_TRUE(graph.get_links().empty());
    EXPECT_EQ(pin_link_count(hub),    0u);
    EXPECT_EQ(pin_link_count(up_0),   0u);
    EXPECT_EQ(pin_link_count(up_1),   0u);
    EXPECT_EQ(pin_link_count(down_0), 0u);
    EXPECT_EQ(pin_link_count(down_1), 0u);

    graph.unregister_node(&up_0);
    graph.unregister_node(&up_1);
    graph.unregister_node(&down_0);
    graph.unregister_node(&down_1);
}

TEST(GraphEdit, FanOutOneSourcePinToTwoSinks)
{
    Graph graph{};
    Simple_node source{"source"};
    Simple_node sink_0{"sink_0"};
    Simple_node sink_1{"sink_1"};
    graph.register_node(&source);
    graph.register_node(&sink_0);
    graph.register_node(&sink_1);

    Link* to_sink_0 = graph.connect(source.output_pin(0), sink_0.input_pin(0));
    Link* to_sink_1 = graph.connect(source.output_pin(0), sink_1.input_pin(0));
    ASSERT_NE(to_sink_0, nullptr);
    ASSERT_NE(to_sink_1, nullptr);

    ASSERT_EQ(source.output_pin(0)->get_links().size(), 2u);
    EXPECT_EQ(source.output_pin(0)->get_links()[0], to_sink_0);
    EXPECT_EQ(source.output_pin(0)->get_links()[1], to_sink_1);
    ASSERT_EQ(sink_0.input_pin(0)->get_links().size(), 1u);
    ASSERT_EQ(sink_1.input_pin(0)->get_links().size(), 1u);

    graph.unregister_node(&source);
    graph.unregister_node(&sink_0);
    graph.unregister_node(&sink_1);
}

TEST(GraphEdit, FanInTwoSourcesIntoOneInputPin)
{
    Graph graph{};
    Simple_node source_0{"source_0"};
    Simple_node source_1{"source_1"};
    Simple_node sink{"sink"};
    graph.register_node(&source_0);
    graph.register_node(&source_1);
    graph.register_node(&sink);

    Link* from_source_0 = graph.connect(source_0.output_pin(0), sink.input_pin(0));
    Link* from_source_1 = graph.connect(source_1.output_pin(0), sink.input_pin(0));
    ASSERT_NE(from_source_0, nullptr);
    ASSERT_NE(from_source_1, nullptr);

    ASSERT_EQ(sink.input_pin(0)->get_links().size(), 2u);
    EXPECT_EQ(sink.input_pin(0)->get_links()[0], from_source_0);
    EXPECT_EQ(sink.input_pin(0)->get_links()[1], from_source_1);

    // Disconnecting one leaves exactly the other on the shared input pin
    graph.disconnect(from_source_0);
    ASSERT_EQ(sink.input_pin(0)->get_links().size(), 1u);
    EXPECT_EQ(sink.input_pin(0)->get_links()[0], from_source_1);
    EXPECT_TRUE(source_0.output_pin(0)->get_links().empty());

    graph.unregister_node(&source_0);
    graph.unregister_node(&source_1);
    graph.unregister_node(&sink);
}

} // namespace erhe::graph::test
