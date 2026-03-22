#include "erhe_item/hierarchy.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using H = erhe::Hierarchy;

auto make(const std::string& name) -> std::shared_ptr<H>
{
    return std::make_shared<H>(name);
}

// --- Basic Construction ---

TEST(Hierarchy, DefaultConstruction)
{
    auto node = make("root");
    EXPECT_TRUE(node->get_parent().expired());
    EXPECT_EQ(node->get_child_count(), 0u);
    EXPECT_EQ(node->get_depth(), 0u);
}

TEST(Hierarchy, NamedConstruction)
{
    auto node = make("test");
    EXPECT_EQ(node->get_name(), "test");
}

// --- Set Parent ---

TEST(Hierarchy, SetParent)
{
    auto parent = make("parent");
    auto child  = make("child");

    child->set_parent(parent);

    EXPECT_EQ(child->get_parent().lock(), parent);
    EXPECT_EQ(parent->get_child_count(), 1u);
    EXPECT_EQ(parent->get_children()[0], child);
}

TEST(Hierarchy, ChildDepth)
{
    auto parent = make("parent");
    auto child  = make("child");
    child->set_parent(parent);
    EXPECT_EQ(child->get_depth(), parent->get_depth() + 1);
}

TEST(Hierarchy, MultipleChildren)
{
    auto parent = make("parent");
    auto c1     = make("c1");
    auto c2     = make("c2");
    auto c3     = make("c3");

    c1->set_parent(parent);
    c2->set_parent(parent);
    c3->set_parent(parent);

    EXPECT_EQ(parent->get_child_count(), 3u);
    EXPECT_EQ(parent->get_children()[0], c1);
    EXPECT_EQ(parent->get_children()[1], c2);
    EXPECT_EQ(parent->get_children()[2], c3);
}

TEST(Hierarchy, SetParentWithPosition)
{
    auto parent = make("parent");
    auto c1     = make("c1");
    auto c2     = make("c2");
    auto c3     = make("c3");

    c1->set_parent(parent);
    c2->set_parent(parent);
    c3->set_parent(parent, 0); // insert at front

    EXPECT_EQ(parent->get_children()[0], c3);
    EXPECT_EQ(parent->get_children()[1], c1);
    EXPECT_EQ(parent->get_children()[2], c2);
}

TEST(Hierarchy, SetParentWithPositionMiddle)
{
    auto parent = make("parent");
    auto c1     = make("c1");
    auto c2     = make("c2");
    auto c3     = make("c3");

    c1->set_parent(parent);
    c2->set_parent(parent);
    c3->set_parent(parent, 1); // insert at position 1

    EXPECT_EQ(parent->get_children()[0], c1);
    EXPECT_EQ(parent->get_children()[1], c3);
    EXPECT_EQ(parent->get_children()[2], c2);
}

TEST(Hierarchy, SetParentNull)
{
    auto parent = make("parent");
    auto child  = make("child");

    child->set_parent(parent);
    EXPECT_FALSE(child->get_parent().expired());

    child->set_parent(std::shared_ptr<H>{});
    EXPECT_TRUE(child->get_parent().expired());
    EXPECT_EQ(child->get_depth(), 0u);
    EXPECT_EQ(parent->get_child_count(), 0u);
}

// --- Re-parenting ---

TEST(Hierarchy, Reparent)
{
    auto parent_a = make("A");
    auto parent_b = make("B");
    auto child    = make("child");

    child->set_parent(parent_a);
    EXPECT_EQ(parent_a->get_child_count(), 1u);
    EXPECT_EQ(parent_b->get_child_count(), 0u);

    child->set_parent(parent_b);
    EXPECT_EQ(parent_a->get_child_count(), 0u);
    EXPECT_EQ(parent_b->get_child_count(), 1u);
    EXPECT_EQ(child->get_parent().lock(), parent_b);
}

TEST(Hierarchy, ReparentUpdateDepth)
{
    auto root       = make("root");
    auto mid        = make("mid");
    auto child      = make("child");
    auto grandchild = make("grandchild");

    mid->set_parent(root);
    child->set_parent(root);       // depth 1
    grandchild->set_parent(child); // depth 2

    EXPECT_EQ(grandchild->get_depth(), 2u);

    // Move child under mid (depth 1), so child becomes depth 2, grandchild depth 3
    child->set_parent(mid);
    EXPECT_EQ(child->get_depth(), 2u);
    EXPECT_EQ(grandchild->get_depth(), 3u);
}

// --- Traversal ---

TEST(Hierarchy, GetRoot)
{
    auto root = make("root");
    auto a    = make("A");
    auto b    = make("B");
    auto c    = make("C");

    a->set_parent(root);
    b->set_parent(a);
    c->set_parent(b);

    EXPECT_EQ(c->get_root().lock(), root);
    EXPECT_EQ(b->get_root().lock(), root);
    EXPECT_EQ(a->get_root().lock(), root);
}

TEST(Hierarchy, GetRootOrphan)
{
    auto node = make("orphan");
    EXPECT_EQ(node->get_root().lock(), node);
}

TEST(Hierarchy, IsAncestor)
{
    auto root = make("root");
    auto a    = make("A");
    auto b    = make("B");

    a->set_parent(root);
    b->set_parent(a);

    EXPECT_TRUE(b->is_ancestor(root.get()));
    EXPECT_TRUE(b->is_ancestor(a.get()));
    EXPECT_FALSE(a->is_ancestor(b.get()));
    EXPECT_FALSE(root->is_ancestor(a.get()));
}

TEST(Hierarchy, IsAncestorOrphan)
{
    auto node  = make("orphan");
    auto other = make("other");
    EXPECT_FALSE(node->is_ancestor(other.get()));
}

TEST(Hierarchy, GetIndexInParent)
{
    auto parent = make("parent");
    auto c0     = make("c0");
    auto c1     = make("c1");
    auto c2     = make("c2");

    c0->set_parent(parent);
    c1->set_parent(parent);
    c2->set_parent(parent);

    EXPECT_EQ(c0->get_index_in_parent(), 0u);
    EXPECT_EQ(c1->get_index_in_parent(), 1u);
    EXPECT_EQ(c2->get_index_in_parent(), 2u);
}

TEST(Hierarchy, GetIndexInParentOrphan)
{
    auto node = make("orphan");
    EXPECT_EQ(node->get_index_in_parent(), 0u);
}

TEST(Hierarchy, GetIndexOfChild)
{
    auto parent = make("parent");
    auto child  = make("child");
    auto other  = make("other");

    child->set_parent(parent);

    auto idx = parent->get_index_of_child(child.get());
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(idx.value(), 0u);

    EXPECT_FALSE(parent->get_index_of_child(other.get()).has_value());
}

TEST(Hierarchy, GetChildCountWithFilter)
{
    auto parent = make("parent");
    auto c1     = make("c1");
    auto c2     = make("c2");
    auto c3     = make("c3");

    c1->set_parent(parent);
    c2->set_parent(parent);
    c3->set_parent(parent);

    c1->show();
    c2->show();
    // c3 not visible

    erhe::Item_filter filter;
    filter.require_all_bits_set = erhe::Item_flags::visible;
    EXPECT_EQ(parent->get_child_count(filter), 2u);
}

// --- Removal ---

TEST(Hierarchy, Remove)
{
    auto root       = make("root");
    auto mid        = make("mid");
    auto grandchild = make("grandchild");

    mid->set_parent(root);
    grandchild->set_parent(mid);

    // Remove mid: grandchild should be reparented to root
    mid->remove();

    EXPECT_TRUE(mid->get_parent().expired());
    EXPECT_EQ(mid->get_child_count(), 0u);
    EXPECT_EQ(root->get_child_count(), 1u);
    EXPECT_EQ(root->get_children()[0], grandchild);
    EXPECT_EQ(grandchild->get_parent().lock(), root);
}

TEST(Hierarchy, RemoveLeaf)
{
    auto parent = make("parent");
    auto leaf   = make("leaf");

    leaf->set_parent(parent);
    EXPECT_EQ(parent->get_child_count(), 1u);

    leaf->remove();
    EXPECT_EQ(parent->get_child_count(), 0u);
    EXPECT_TRUE(leaf->get_parent().expired());
}

TEST(Hierarchy, RecursiveRemove)
{
    auto root       = make("root");
    auto mid        = make("mid");
    auto grandchild = make("grandchild");

    mid->set_parent(root);
    grandchild->set_parent(mid);

    mid->recursive_remove();

    EXPECT_EQ(root->get_child_count(), 0u);
    EXPECT_TRUE(mid->get_parent().expired());
    EXPECT_TRUE(grandchild->get_parent().expired());
    EXPECT_EQ(mid->get_child_count(), 0u);
}

TEST(Hierarchy, RemoveAllChildrenRecursively)
{
    auto parent     = make("parent");
    auto c1         = make("c1");
    auto c2         = make("c2");
    auto grandchild = make("gc");

    c1->set_parent(parent);
    c2->set_parent(parent);
    grandchild->set_parent(c1);

    parent->remove_all_children_recursively();
    EXPECT_EQ(parent->get_child_count(), 0u);
}

// --- Iteration ---

TEST(Hierarchy, ForEach)
{
    auto root = make("root");
    auto a    = make("A");
    auto b    = make("B");
    auto c    = make("C");

    a->set_parent(root);
    b->set_parent(root);
    c->set_parent(a);

    std::vector<std::string> visited;
    root->for_each<H>([&](H& node) -> bool {
        visited.push_back(std::string{node.get_name()});
        return true;
    });

    ASSERT_EQ(visited.size(), 4u);
    EXPECT_EQ(visited[0], "root");
    EXPECT_EQ(visited[1], "A");
    EXPECT_EQ(visited[2], "C"); // child of A
    EXPECT_EQ(visited[3], "B");
}

TEST(Hierarchy, ForEachChild)
{
    auto root = make("root");
    auto a    = make("A");
    auto b    = make("B");

    a->set_parent(root);
    b->set_parent(root);

    std::vector<std::string> visited;
    root->for_each_child<H>([&](H& node) -> bool {
        visited.push_back(std::string{node.get_name()});
        return true;
    });

    ASSERT_EQ(visited.size(), 2u);
    EXPECT_EQ(visited[0], "A");
    EXPECT_EQ(visited[1], "B");
}

TEST(Hierarchy, ForEachReturnFalseStops)
{
    auto root = make("root");
    auto a    = make("A");
    auto b    = make("B");
    auto c    = make("C");

    a->set_parent(root);
    b->set_parent(root);
    c->set_parent(root);

    std::vector<std::string> visited;
    root->for_each<H>([&](H& node) -> bool {
        visited.push_back(std::string{node.get_name()});
        return visited.size() < 2; // stop after 2
    });

    EXPECT_EQ(visited.size(), 2u);
}

// --- Copy ---

TEST(Hierarchy, CopyConstructor)
{
    auto root  = make("root");
    auto child = make("child");
    child->set_parent(root);

    auto copy = std::make_shared<H>(*root);
    copy->adopt_orphan_children();

    EXPECT_EQ(copy->get_child_count(), 1u);
    EXPECT_NE(copy->get_children()[0], child); // independent copy
    EXPECT_EQ(copy->get_children()[0]->get_name(), "child Copy");
}

TEST(Hierarchy, CopyDoesNotCopyParent)
{
    auto parent = make("parent");
    auto child  = make("child");
    child->set_parent(parent);

    auto copy = std::make_shared<H>(*child);
    EXPECT_TRUE(copy->get_parent().expired());
}

// --- Sanity Check ---

TEST(Hierarchy, SanityCheckPasses)
{
    auto root = make("root");
    auto a    = make("A");
    auto b    = make("B");

    a->set_parent(root);
    b->set_parent(a);

    // Should not crash or assert
    root->hierarchy_sanity_check();
}

} // namespace
