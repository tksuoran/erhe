#include "test_node.hpp"

#include <gtest/gtest.h>

namespace erhe::graph::test {

TEST(GraphConnect, AcceptsKeyMatchedAcyclicLink)
{
    Graph graph{};
    Simple_node a{"a"};
    Simple_node b{"b"};
    graph.register_node(&a);
    graph.register_node(&b);

    Link* link = graph.connect(a.output_pin(0), b.input_pin(0));
    ASSERT_NE(link, nullptr);
    EXPECT_TRUE(link->is_connected());
    EXPECT_EQ(link->get_source(), a.output_pin(0));
    EXPECT_EQ(link->get_sink(),   b.input_pin(0));

    // Link is registered on both pins
    ASSERT_EQ(a.output_pin(0)->get_links().size(), 1u);
    ASSERT_EQ(b.input_pin(0)->get_links().size(), 1u);
    EXPECT_EQ(a.output_pin(0)->get_links()[0], link);
    EXPECT_EQ(b.input_pin(0)->get_links()[0], link);

    // Link is owned by the graph
    ASSERT_EQ(graph.get_links().size(), 1u);
    EXPECT_EQ(graph.get_links()[0].get(), link);

    graph.unregister_node(&a);
    graph.unregister_node(&b);
}

TEST(GraphConnect, RefusesKeyMismatch)
{
    Graph graph{};
    Test_node a{"a"};
    Test_node b{"b"};
    a.add_output_pin(key_alpha, "out_alpha");
    b.add_input_pin (key_beta,  "in_beta");
    graph.register_node(&a);
    graph.register_node(&b);

    Link* link = graph.connect(a.output_pin(0), b.input_pin(0));
    EXPECT_EQ(link, nullptr);
    EXPECT_TRUE(graph.get_links().empty());
    EXPECT_TRUE(a.output_pin(0)->get_links().empty());
    EXPECT_TRUE(b.input_pin(0)->get_links().empty());

    graph.unregister_node(&a);
    graph.unregister_node(&b);
}

TEST(GraphConnect, RefusesSelfLink)
{
    Graph graph{};
    Simple_node a{"a"};
    graph.register_node(&a);

    EXPECT_TRUE(graph.would_create_cycle(a.output_pin(0), a.input_pin(0)));

    Link* link = graph.connect(a.output_pin(0), a.input_pin(0));
    EXPECT_EQ(link, nullptr);
    EXPECT_TRUE(graph.get_links().empty());
    EXPECT_EQ(pin_link_count(a), 0u);

    graph.unregister_node(&a);
}

TEST(GraphConnect, RefusesTwoNodeCycle)
{
    Graph graph{};
    Simple_node a{"a"};
    Simple_node b{"b"};
    graph.register_node(&a);
    graph.register_node(&b);

    // No cycle before any links exist
    EXPECT_FALSE(graph.would_create_cycle(a.output_pin(0), b.input_pin(0)));

    Link* forward = graph.connect(a.output_pin(0), b.input_pin(0));
    ASSERT_NE(forward, nullptr);

    // b -> a would close a 2-node cycle
    EXPECT_TRUE(graph.would_create_cycle(b.output_pin(0), a.input_pin(0)));
    Link* backward = graph.connect(b.output_pin(0), a.input_pin(0));
    EXPECT_EQ(backward, nullptr);
    EXPECT_EQ(graph.get_links().size(), 1u);
    EXPECT_EQ(pin_link_count(a), 1u);
    EXPECT_EQ(pin_link_count(b), 1u);

    graph.unregister_node(&a);
    graph.unregister_node(&b);
}

TEST(GraphConnect, RefusesThreeNodeCycle)
{
    Graph graph{};
    Simple_node a{"a"};
    Simple_node b{"b"};
    Simple_node c{"c"};
    graph.register_node(&a);
    graph.register_node(&b);
    graph.register_node(&c);

    ASSERT_NE(graph.connect(a.output_pin(0), b.input_pin(0)), nullptr);
    ASSERT_NE(graph.connect(b.output_pin(0), c.input_pin(0)), nullptr);

    // c -> a would close a 3-node cycle a -> b -> c -> a
    EXPECT_TRUE(graph.would_create_cycle(c.output_pin(0), a.input_pin(0)));
    Link* closing = graph.connect(c.output_pin(0), a.input_pin(0));
    EXPECT_EQ(closing, nullptr);
    EXPECT_EQ(graph.get_links().size(), 2u);

    graph.unregister_node(&a);
    graph.unregister_node(&b);
    graph.unregister_node(&c);
}

TEST(GraphConnect, DiamondIsNotACycle)
{
    Graph graph{};
    Simple_node a{"a"};
    Simple_node b{"b"};
    Simple_node c{"c"};
    Simple_node d{"d"};
    graph.register_node(&a);
    graph.register_node(&b);
    graph.register_node(&c);
    graph.register_node(&d);

    ASSERT_NE(graph.connect(a.output_pin(0), b.input_pin(0)), nullptr);
    ASSERT_NE(graph.connect(a.output_pin(0), c.input_pin(0)), nullptr);
    ASSERT_NE(graph.connect(b.output_pin(0), d.input_pin(0)), nullptr);

    // Closing the diamond c -> d is legal: d is not upstream of c
    EXPECT_FALSE(graph.would_create_cycle(c.output_pin(0), d.input_pin(0)));
    Link* closing = graph.connect(c.output_pin(0), d.input_pin(0));
    EXPECT_NE(closing, nullptr);
    EXPECT_EQ(graph.get_links().size(), 4u);

    // But d -> a (any edge back into the diamond's source) is a cycle
    EXPECT_TRUE(graph.would_create_cycle(d.output_pin(0), a.input_pin(0)));

    graph.unregister_node(&a);
    graph.unregister_node(&b);
    graph.unregister_node(&c);
    graph.unregister_node(&d);
}

} // namespace erhe::graph::test
