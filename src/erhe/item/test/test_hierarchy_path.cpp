#include "erhe_item/hierarchy.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using H = erhe::Hierarchy;

auto make(const std::string& name) -> std::shared_ptr<H>
{
    return std::make_shared<H>(name);
}

// The path form (doc/usd-compatibility-plan.md M1): names from the root,
// the root's own name excluded, separated by '/'.

TEST(Hierarchy_path, RootPathIsEmpty)
{
    std::shared_ptr<H> root = make("root");
    EXPECT_EQ(root->get_path(), "");
}

TEST(Hierarchy_path, RootReferencePathIsItsName)
{
    std::shared_ptr<H> root = make("root");
    EXPECT_EQ(root->get_reference_path(), "root");
}

TEST(Hierarchy_path, ChildOfRootPathIsItsName)
{
    std::shared_ptr<H> root  = make("root");
    std::shared_ptr<H> child = make("child");
    child->set_parent(root);
    EXPECT_EQ(child->get_path(), "child");
    EXPECT_EQ(child->get_reference_path(), "child");
}

TEST(Hierarchy_path, DeepPath)
{
    std::shared_ptr<H> root   = make("root");
    std::shared_ptr<H> parent = make("parent");
    std::shared_ptr<H> child  = make("child");
    std::shared_ptr<H> leaf   = make("leaf");
    parent->set_parent(root);
    child->set_parent(parent);
    leaf->set_parent(child);
    EXPECT_EQ(leaf->get_path(), "parent/child/leaf");
    EXPECT_EQ(leaf->get_reference_path(), "parent/child/leaf");
}

TEST(Hierarchy_path, OrphanPathIsEmptyAndReferenceIsName)
{
    std::shared_ptr<H> orphan = make("orphan");
    EXPECT_EQ(orphan->get_path(), "");
    EXPECT_EQ(orphan->get_reference_path(), "orphan");
}

TEST(Hierarchy_path, PathFollowsRename)
{
    std::shared_ptr<H> root   = make("root");
    std::shared_ptr<H> parent = make("parent");
    std::shared_ptr<H> child  = make("child");
    parent->set_parent(root);
    child->set_parent(parent);
    EXPECT_EQ(child->get_path(), "parent/child");
    parent->set_name("renamed");
    EXPECT_EQ(child->get_path(), "renamed/child");
    child->set_name("leaf");
    EXPECT_EQ(child->get_path(), "renamed/leaf");
}

TEST(Hierarchy_path, PathFollowsReparent)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> a    = make("a");
    std::shared_ptr<H> b    = make("b");
    std::shared_ptr<H> item = make("item");
    a->set_parent(root);
    b->set_parent(root);
    item->set_parent(a);
    EXPECT_EQ(item->get_path(), "a/item");
    item->set_parent(b);
    EXPECT_EQ(item->get_path(), "b/item");
    item->set_parent(root);
    EXPECT_EQ(item->get_path(), "item");
}

// --- find_by_path ---

TEST(Hierarchy_path, FindByEmptyPathIsRoot)
{
    std::shared_ptr<H> root = make("root");
    EXPECT_EQ(erhe::find_by_path(*root, ""), root.get());
}

TEST(Hierarchy_path, FindByPath)
{
    std::shared_ptr<H> root   = make("root");
    std::shared_ptr<H> parent = make("parent");
    std::shared_ptr<H> child  = make("child");
    std::shared_ptr<H> other  = make("child");
    parent->set_parent(root);
    child->set_parent(parent);
    other->set_parent(root);
    EXPECT_EQ(erhe::find_by_path(*root, "parent"), parent.get());
    EXPECT_EQ(erhe::find_by_path(*root, "parent/child"), child.get());
    // Two items of one name are told apart by their paths.
    EXPECT_EQ(erhe::find_by_path(*root, "child"), other.get());
}

TEST(Hierarchy_path, FindByPathRoundTrip)
{
    std::shared_ptr<H> root   = make("root");
    std::shared_ptr<H> parent = make("parent");
    std::shared_ptr<H> leaf   = make("leaf");
    parent->set_parent(root);
    leaf->set_parent(parent);
    EXPECT_EQ(erhe::find_by_path(*root, leaf->get_path()), leaf.get());
}

TEST(Hierarchy_path, FindByPathMissingSegment)
{
    std::shared_ptr<H> root   = make("root");
    std::shared_ptr<H> parent = make("parent");
    parent->set_parent(root);
    EXPECT_EQ(erhe::find_by_path(*root, "missing"), nullptr);
    EXPECT_EQ(erhe::find_by_path(*root, "parent/missing"), nullptr);
    // The root's own name is not part of a path.
    EXPECT_EQ(erhe::find_by_path(*root, "root/parent"), nullptr);
    // An empty segment names no child.
    EXPECT_EQ(erhe::find_by_path(*root, "/parent"), nullptr);
}

TEST(Hierarchy_path, FindByPathAfterReparent)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> a    = make("a");
    std::shared_ptr<H> b    = make("b");
    std::shared_ptr<H> item = make("item");
    a->set_parent(root);
    b->set_parent(root);
    item->set_parent(a);
    EXPECT_EQ(erhe::find_by_path(*root, "a/item"), item.get());
    item->set_parent(b);
    EXPECT_EQ(erhe::find_by_path(*root, "a/item"), nullptr);
    EXPECT_EQ(erhe::find_by_path(*root, "b/item"), item.get());
}

TEST(Hierarchy_path, FindByPathAfterRename)
{
    std::shared_ptr<H> root  = make("root");
    std::shared_ptr<H> child = make("child");
    child->set_parent(root);
    child->set_name("renamed");
    EXPECT_EQ(erhe::find_by_path(*root, "child"), nullptr);
    EXPECT_EQ(erhe::find_by_path(*root, "renamed"), child.get());
}

// A bare name is the older stored reference form and still names a child of
// the root, which is exactly that child's path.
TEST(Hierarchy_path, BareNameFallbackIsAChildPath)
{
    std::shared_ptr<H> root  = make("root");
    std::shared_ptr<H> child = make("child");
    child->set_parent(root);
    EXPECT_EQ(child->get_reference_path(), "child");
    EXPECT_EQ(erhe::find_by_path(*root, "child"), child.get());
}

} // anonymous namespace
