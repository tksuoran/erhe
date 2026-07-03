#include "test_node.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace erhe::graph::test {

namespace {

[[nodiscard]] auto index_of(const std::vector<Node*>& nodes, const Node* node) -> std::size_t
{
    const std::vector<Node*>::const_iterator i = std::find(nodes.begin(), nodes.end(), node);
    EXPECT_NE(i, nodes.end());
    return static_cast<std::size_t>(std::distance(nodes.begin(), i));
}

// True when every link's source node precedes its sink node in the graph's
// node order - the definition of a valid topological order.
[[nodiscard]] auto is_topologically_ordered(Graph& graph) -> bool
{
    const std::vector<Node*>& nodes = graph.get_nodes();
    for (const std::unique_ptr<Link>& link : graph.get_links()) {
        const Node* source_node = link->get_source()->get_owner_node();
        const Node* sink_node   = link->get_sink  ()->get_owner_node();
        if (index_of(nodes, source_node) >= index_of(nodes, sink_node)) {
            return false;
        }
    }
    return true;
}

void unregister_all(Graph& graph, std::vector<Node*> nodes)
{
    for (Node* node : nodes) {
        graph.unregister_node(node);
    }
}

} // anonymous namespace

TEST(GraphSort, ChainSortsSourceBeforeSink)
{
    Graph graph{};
    Simple_node a{"a"};
    Simple_node b{"b"};
    Simple_node c{"c"};
    // Register in reverse dependency order so a pre-sorted vector cannot
    // pass by accident.
    graph.register_node(&c);
    graph.register_node(&b);
    graph.register_node(&a);

    ASSERT_NE(graph.connect(a.output_pin(0), b.input_pin(0)), nullptr);
    ASSERT_NE(graph.connect(b.output_pin(0), c.input_pin(0)), nullptr);

    ASSERT_FALSE(is_topologically_ordered(graph)); // registration order c, b, a
    graph.sort();
    EXPECT_TRUE(graph.m_is_sorted);
    EXPECT_EQ(graph.get_nodes().size(), 3u);
    EXPECT_TRUE(is_topologically_ordered(graph));

    unregister_all(graph, {&a, &b, &c});
}

TEST(GraphSort, DiamondSortsAllLinksForward)
{
    Graph graph{};
    Simple_node a{"a"};
    Simple_node b{"b"};
    Simple_node c{"c"};
    Simple_node d{"d"};
    graph.register_node(&d);
    graph.register_node(&c);
    graph.register_node(&b);
    graph.register_node(&a);

    ASSERT_NE(graph.connect(a.output_pin(0), b.input_pin(0)), nullptr);
    ASSERT_NE(graph.connect(a.output_pin(0), c.input_pin(0)), nullptr);
    ASSERT_NE(graph.connect(b.output_pin(0), d.input_pin(0)), nullptr);
    ASSERT_NE(graph.connect(c.output_pin(0), d.input_pin(0)), nullptr);

    graph.sort();
    EXPECT_TRUE(graph.m_is_sorted);
    EXPECT_EQ(graph.get_nodes().size(), 4u);
    EXPECT_TRUE(is_topologically_ordered(graph));
    EXPECT_EQ(index_of(graph.get_nodes(), &a), 0u);
    EXPECT_EQ(index_of(graph.get_nodes(), &d), 3u);

    unregister_all(graph, {&a, &b, &c, &d});
}

TEST(GraphSort, MultiRootGraphWithIsolatedNode)
{
    Graph graph{};
    Simple_node root_0{"root_0"};
    Simple_node root_1{"root_1"};
    Simple_node shared_sink{"shared_sink"};
    Simple_node isolated{"isolated"};
    graph.register_node(&shared_sink);
    graph.register_node(&isolated);
    graph.register_node(&root_0);
    graph.register_node(&root_1);

    ASSERT_NE(graph.connect(root_0.output_pin(0), shared_sink.input_pin(0)), nullptr);
    ASSERT_NE(graph.connect(root_1.output_pin(0), shared_sink.input_pin(0)), nullptr);

    graph.sort();
    EXPECT_TRUE(graph.m_is_sorted);
    EXPECT_EQ(graph.get_nodes().size(), 4u);
    EXPECT_TRUE(is_topologically_ordered(graph));

    // Every registered node survives the sort
    EXPECT_NE(std::find(graph.get_nodes().begin(), graph.get_nodes().end(), &isolated), graph.get_nodes().end());

    unregister_all(graph, {&root_0, &root_1, &shared_sink, &isolated});
}

TEST(GraphSort, StructuralEditsInvalidateSortedness)
{
    Graph graph{};
    Simple_node a{"a"};
    Simple_node b{"b"};
    graph.register_node(&b);
    graph.register_node(&a);

    Link* link = graph.connect(a.output_pin(0), b.input_pin(0));
    ASSERT_NE(link, nullptr);

    graph.sort();
    EXPECT_TRUE(graph.m_is_sorted);

    // disconnect invalidates
    graph.disconnect(link);
    EXPECT_FALSE(graph.m_is_sorted);
    graph.sort();
    EXPECT_TRUE(graph.m_is_sorted);

    // connect invalidates
    link = graph.connect(a.output_pin(0), b.input_pin(0));
    ASSERT_NE(link, nullptr);
    EXPECT_FALSE(graph.m_is_sorted);
    graph.sort();
    EXPECT_TRUE(graph.m_is_sorted);

    // register_node invalidates
    Simple_node c{"c"};
    graph.register_node(&c);
    EXPECT_FALSE(graph.m_is_sorted);
    graph.sort();
    EXPECT_TRUE(graph.m_is_sorted);

    // unregister_node invalidates
    graph.unregister_node(&c);
    EXPECT_FALSE(graph.m_is_sorted);

    unregister_all(graph, {&a, &b});
}

TEST(GraphSort, ResortAfterEditYieldsValidOrder)
{
    Graph graph{};
    Simple_node a{"a"};
    Simple_node b{"b"};
    Simple_node c{"c"};
    graph.register_node(&a);
    graph.register_node(&b);
    graph.register_node(&c);

    ASSERT_NE(graph.connect(a.output_pin(0), b.input_pin(0)), nullptr);
    graph.sort();
    EXPECT_TRUE(is_topologically_ordered(graph));

    // New upstream dependency: c -> a. c must move before a on re-sort.
    ASSERT_NE(graph.connect(c.output_pin(0), a.input_pin(0)), nullptr);
    EXPECT_FALSE(graph.m_is_sorted);
    graph.sort();
    EXPECT_TRUE(graph.m_is_sorted);
    EXPECT_TRUE(is_topologically_ordered(graph));
    EXPECT_LT(index_of(graph.get_nodes(), &c), index_of(graph.get_nodes(), &a));

    unregister_all(graph, {&a, &b, &c});
}

} // namespace erhe::graph::test
