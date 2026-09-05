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

// Sibling-unique names (doc/usd-compatibility-plan.md M2): the children of one
// parent hold distinct names. Attaching applies the numeric suffix; renaming an
// attached item to a name a sibling holds is refused.

TEST(Hierarchy_unique_names, FreeNameIsKept)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> a    = make("Cube");
    a->set_parent(root);
    EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Sphere", nullptr), "Sphere");
}

TEST(Hierarchy_unique_names, NoParentImposesNoNamespace)
{
    EXPECT_EQ(H::make_sibling_unique_name(nullptr, "Cube", nullptr), "Cube");
}

TEST(Hierarchy_unique_names, FirstCollisionGetsSuffixOne)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> a    = make("Cube");
    a->set_parent(root);
    EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube", nullptr), "Cube_1");
}

TEST(Hierarchy_unique_names, SuffixedNameContinuesTheSeries)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> a    = make("Cube");
    std::shared_ptr<H> b    = make("Cube");
    a->set_parent(root);
    b->set_parent(root);
    EXPECT_EQ(b->get_name(), "Cube_1");
    // A wanted 'Cube_1' is a 'Cube' with a counter, so it yields 'Cube_2'.
    EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube_1", nullptr), "Cube_2");
}

TEST(Hierarchy_unique_names, GapInTheSeriesIsFilled)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> a    = make("Cube");
    std::shared_ptr<H> b    = make("Cube_2");
    a->set_parent(root);
    b->set_parent(root);
    EXPECT_EQ(b->get_name(), "Cube_2");
    EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube", nullptr), "Cube_1");
}

TEST(Hierarchy_unique_names, NameThatIsOnlyACounterIsItsOwnBase)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> a    = make("_1");
    a->set_parent(root);
    EXPECT_EQ(H::make_sibling_unique_name(root.get(), "_1", nullptr), "_1_1");
}

TEST(Hierarchy_unique_names, ExcludedSiblingDoesNotCollide)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> a    = make("Cube");
    a->set_parent(root);
    EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube", a.get()), "Cube");
}

TEST(Hierarchy_unique_names, AttachRenamesTheCollidingChild)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> a    = make("Cube");
    std::shared_ptr<H> b    = make("Cube");
    std::shared_ptr<H> c    = make("Cube");
    a->set_parent(root);
    b->set_parent(root);
    c->set_parent(root);
    EXPECT_EQ(a->get_name(), "Cube");
    EXPECT_EQ(b->get_name(), "Cube_1");
    EXPECT_EQ(c->get_name(), "Cube_2");
    EXPECT_EQ(c->get_path(), "Cube_2");
}

TEST(Hierarchy_unique_names, DifferentParentsAreDifferentNamespaces)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> p    = make("Parent");
    std::shared_ptr<H> a    = make("Cube");
    std::shared_ptr<H> b    = make("Cube");
    p->set_parent(root);
    a->set_parent(root);
    b->set_parent(p);
    EXPECT_EQ(a->get_name(), "Cube");
    EXPECT_EQ(b->get_name(), "Cube");
    EXPECT_EQ(b->get_path(), "Parent/Cube");
}

TEST(Hierarchy_unique_names, ReparentIntoACollidingParentRenames)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> p    = make("Parent");
    std::shared_ptr<H> a    = make("Cube");
    std::shared_ptr<H> b    = make("Cube");
    p->set_parent(root);
    a->set_parent(p);
    b->set_parent(root);
    EXPECT_EQ(b->get_name(), "Cube");
    b->set_parent(p);
    EXPECT_EQ(b->get_name(), "Cube_1");
    EXPECT_EQ(b->get_path(), "Parent/Cube_1");
}

TEST(Hierarchy_unique_names, DetachAndReattachKeepsTheAssignedName)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> a    = make("Cube");
    std::shared_ptr<H> b    = make("Cube");
    a->set_parent(root);
    b->set_parent(root);
    EXPECT_EQ(b->get_name(), "Cube_1");
    b->set_parent(std::shared_ptr<H>{}); // undo of an insert
    EXPECT_EQ(b->get_name(), "Cube_1");
    b->set_parent(root);                 // redo
    EXPECT_EQ(b->get_name(), "Cube_1");
}

TEST(Hierarchy_unique_names, IsNameAvailableSeesSiblingsOnly)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> p    = make("Parent");
    std::shared_ptr<H> a    = make("Cube");
    std::shared_ptr<H> b    = make("Sphere");
    p->set_parent(root);
    a->set_parent(root);
    b->set_parent(p);
    EXPECT_FALSE(a->is_name_available("Parent"));
    EXPECT_TRUE (a->is_name_available("Cube"));   // its own name
    EXPECT_TRUE (a->is_name_available("Sphere")); // a child of Parent
    EXPECT_TRUE (root->is_name_available("Cube"));
}

TEST(Hierarchy_unique_names, RenameIntoACollisionIsRefused)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> a    = make("Cube");
    std::shared_ptr<H> b    = make("Sphere");
    a->set_parent(root);
    b->set_parent(root);
    EXPECT_FALSE(b->set_value(erhe::Item_base::name_property.get(), erhe::property::Property_value{std::string{"Cube"}}));
    EXPECT_EQ(b->get_name(), "Sphere");
    EXPECT_TRUE(b->set_value(erhe::Item_base::name_property.get(), erhe::property::Property_value{std::string{"Cylinder"}}));
    EXPECT_EQ(b->get_name(), "Cylinder");
}

} // anonymous namespace
