#include "erhe_item/hierarchy.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using H = erhe::Hierarchy;

auto make(const std::string& name) -> std::shared_ptr<H>
{
    return std::make_shared<H>(name);
}

// Sibling-unique names (doc/erhe/usd_compatibility_design.md M2): the children of one
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

// The child name index (doc/erhe/item.md "Sibling-unique names"): above c_child_name_index_build_count children a
// parent answers from its index. The reference is the rule itself, a scan over the children.
[[nodiscard]] auto reference_name(const H& parent, const std::string_view wanted, const H* const exclude) -> std::string
{
    return H::make_unique_name(
        wanted,
        [&parent, exclude](const std::string_view candidate) -> bool {
            for (const std::shared_ptr<H>& child : parent.get_children()) {
                if ((child.get() != exclude) && (child->get_name() == candidate)) {
                    return true;
                }
            }
            return false;
        }
    );
}

void expect_index_agrees(const H& parent, const std::string_view wanted, const H* const exclude = nullptr)
{
    EXPECT_TRUE(parent.is_child_name_index_consistent()) << parent.get_child_count() << " children";
    EXPECT_EQ(H::make_sibling_unique_name(&parent, wanted, exclude), reference_name(parent, wanted, exclude)) << "wanted '" << wanted << "'";
}

auto attach(const std::shared_ptr<H>& parent, const std::string& name) -> std::shared_ptr<H>
{
    std::shared_ptr<H> child = make(name);
    child->set_parent(parent);
    EXPECT_TRUE(parent->is_child_name_index_consistent());
    return child;
}

void add_fillers(const std::shared_ptr<H>& parent, std::vector<std::shared_ptr<H>>& out_children, const std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        out_children.push_back(attach(parent, "filler"));
    }
}

// A sibling-unique insert costs the same whatever the sibling count; 30 s is far above the measured 0.94 s, a guard only.
TEST(Hierarchy_unique_names, ManyCollidingChildrenTakeTheSeries)
{
    constexpr std::size_t count = 5000;
    std::shared_ptr<H> root = make("root");
    std::vector<std::shared_ptr<H>> children;
    children.reserve(count);
    const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        children.push_back(make("Grass"));
        children.back()->set_parent(root);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    EXPECT_LT(seconds, 30.0);
    std::unordered_set<std::string> names;
    for (std::size_t i = 0; i < count; ++i) {
        const std::string expected = (i == 0) ? std::string{"Grass"} : ("Grass_" + std::to_string(i));
        EXPECT_EQ(children[i]->get_name(), expected);
        names.insert(children[i]->get_name());
    }
    EXPECT_EQ(names.size(), count);
    expect_index_agrees(*root, "Grass");
}

TEST(Hierarchy_unique_names, RemovedChildFreesItsSuffix)
{
    std::shared_ptr<H> root = make("root");
    std::vector<std::shared_ptr<H>> children;
    for (int i = 0; i < 6; ++i) {
        children.push_back(attach(root, "Cube"));
    }
    EXPECT_EQ(children[5]->get_name(), "Cube_5");
    children[3]->set_parent(std::shared_ptr<H>{});
    children[1]->set_parent(std::shared_ptr<H>{});
    EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube", nullptr), "Cube_1");
    EXPECT_EQ(attach(root, "Cube")->get_name(), "Cube_1");
    EXPECT_EQ(attach(root, "Cube")->get_name(), "Cube_3");
    EXPECT_EQ(attach(root, "Cube")->get_name(), "Cube_6");
    children[0]->set_parent(std::shared_ptr<H>{});
    EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube", nullptr), "Cube");
}

// The removal pattern of the review: a_1 .. a_N held, a_1 removed, then two attaches of "a".
TEST(Hierarchy_unique_names, RemovalThenTwoAttaches)
{
    std::shared_ptr<H> root = make("root");
    std::vector<std::shared_ptr<H>> children;
    for (int i = 0; i < 41; ++i) {
        children.push_back(attach(root, "a"));
    }
    EXPECT_EQ(children[40]->get_name(), "a_40");
    children[1]->set_parent(std::shared_ptr<H>{});
    expect_index_agrees(*root, "a");
    EXPECT_EQ(attach(root, "a")->get_name(), "a_1");
    expect_index_agrees(*root, "a");
    EXPECT_EQ(attach(root, "a")->get_name(), "a_41");
    expect_index_agrees(*root, "a");
}

// Across the build and drop counts, in both directions, the names are the rule's.
TEST(Hierarchy_unique_names, IndexBuildAndDropKeepTheRule)
{
    std::shared_ptr<H> root = make("root");
    std::vector<std::shared_ptr<H>> children;
    const char* const names[] = {"Cube", "Cube_2", "Sphere", "Cube", "_1", "Cube_1"};
    for (std::size_t i = 0; i < 40; ++i) {
        children.push_back(attach(root, names[i % 6]));
        expect_index_agrees(*root, "Cube");
        expect_index_agrees(*root, "Cube_1");
        expect_index_agrees(*root, "_1");
    }
    // Removal from the middle and the ends, down to none.
    for (std::size_t step = 0; !children.empty(); ++step) {
        const std::size_t index = (step * 7) % children.size();
        children[index]->set_parent(std::shared_ptr<H>{});
        children.erase(children.begin() + static_cast<std::ptrdiff_t>(index));
        expect_index_agrees(*root, "Cube");
        expect_index_agrees(*root, "Sphere");
        if ((children.size() % 5) == 0) {
            children.push_back(attach(root, "Cube"));
            expect_index_agrees(*root, "Cube");
            children.back()->set_parent(std::shared_ptr<H>{});
            children.pop_back();
        }
    }
    EXPECT_EQ(root->get_child_count(), 0u);
    EXPECT_TRUE(root->is_child_name_index_consistent());
}

TEST(Hierarchy_unique_names, RenameOfAnAttachedChildMovesItsName)
{
    for (const std::size_t fillers : {std::size_t{0}, std::size_t{20}}) {
        std::shared_ptr<H> root = make("root");
        std::vector<std::shared_ptr<H>> children;
        add_fillers(root, children, fillers);
        std::shared_ptr<H> a = attach(root, "Cube");
        std::shared_ptr<H> b = attach(root, "Cube");
        EXPECT_EQ(b->get_name(), "Cube_1");
        b->set_name("Sphere");
        expect_index_agrees(*root, "Cube");
        EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube", nullptr), "Cube_1");
        EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Sphere", nullptr), "Sphere_1");
        EXPECT_FALSE(a->is_name_available("Sphere"));
        a->set_name("Cone");
        expect_index_agrees(*root, "Cube");
        EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube", nullptr), "Cube");
        EXPECT_TRUE(b->is_name_available("Cube"));
    }
}

// A name set past the rule (set_name does not refuse): two holders, then one leaves.
TEST(Hierarchy_unique_names, TwoHoldersOfOneName)
{
    std::shared_ptr<H> root = make("root");
    std::vector<std::shared_ptr<H>> children;
    add_fillers(root, children, 20);
    std::shared_ptr<H> a = attach(root, "Cube");
    std::shared_ptr<H> b = attach(root, "Sphere");
    b->set_name("Cube");
    expect_index_agrees(*root, "Cube");
    EXPECT_FALSE(a->is_name_available("Cube"));
    EXPECT_FALSE(b->is_name_available("Cube"));
    b->set_parent(std::shared_ptr<H>{});
    expect_index_agrees(*root, "Cube");
    EXPECT_TRUE(a->is_name_available("Cube"));
    EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube", nullptr), "Cube_1");
    a->set_parent(std::shared_ptr<H>{});
    expect_index_agrees(*root, "Cube");
    EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube", nullptr), "Cube");
}

TEST(Hierarchy_unique_names, ReparentFreesTheNameInTheOldParent)
{
    std::shared_ptr<H> root = make("root");
    std::shared_ptr<H> a    = attach(root, "A");
    std::shared_ptr<H> b    = attach(root, "B");
    std::vector<std::shared_ptr<H>> children;
    add_fillers(a, children, 20);
    add_fillers(b, children, 20);
    std::shared_ptr<H> cube = attach(a, "Cube");
    attach(b, "Cube");
    cube->set_parent(b);
    EXPECT_EQ(cube->get_name(), "Cube_1");
    expect_index_agrees(*a, "Cube");
    expect_index_agrees(*b, "Cube");
    EXPECT_EQ(H::make_sibling_unique_name(a.get(), "Cube", nullptr), "Cube");
    EXPECT_EQ(H::make_sibling_unique_name(b.get(), "Cube", nullptr), "Cube_2");
}

TEST(Hierarchy_unique_names, ExcludedSiblingFreesItsSuffix)
{
    for (const std::size_t fillers : {std::size_t{0}, std::size_t{20}}) {
        std::shared_ptr<H> root = make("root");
        std::vector<std::shared_ptr<H>> children;
        add_fillers(root, children, fillers);
        std::shared_ptr<H> a = attach(root, "Cube");
        std::shared_ptr<H> b = attach(root, "Cube");
        std::shared_ptr<H> c = attach(root, "Cube");
        EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube", nullptr), "Cube_3");
        EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube", b.get()), "Cube_1");
        EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube_2", c.get()), "Cube_2");
        expect_index_agrees(*root, "Cube", a.get());
        expect_index_agrees(*root, "Cube", b.get());
        expect_index_agrees(*root, "Cube_1", c.get());
    }
}

// Suffixes the rule never makes ("a_01", "a_0", a 30-digit one) and bases that are empty or end in a counter.
TEST(Hierarchy_unique_names, SuffixEdgesThroughTheIndex)
{
    const std::string long_suffix = "a_" + std::string(30, '7');
    const std::string names[] = {"a_01", "a_0", "", "_5", "a_1_2", long_suffix, "a", "", "_5", "a_1_2", "a_01", long_suffix, "a"};
    std::shared_ptr<H> root = make("root");
    std::vector<std::shared_ptr<H>> children;
    add_fillers(root, children, 20);
    for (const std::string& name : names) {
        for (const std::string& wanted : names) {
            expect_index_agrees(*root, wanted);
        }
        const std::string expected = reference_name(*root, name, nullptr);
        children.push_back(attach(root, name));
        EXPECT_EQ(children.back()->get_name(), expected);
    }
    EXPECT_EQ(children[27]->get_name(), "_1");    // a colliding "" starts the "" series
    EXPECT_EQ(children[28]->get_name(), "_5_1");  // "_5" is its own base
    EXPECT_EQ(children[29]->get_name(), "a_1_1"); // "a_1_2" is "a_1" with a counter
    EXPECT_EQ(children[32]->get_name(), "a_3");   // a_1 from "a_01", a_2 from the 30-digit suffix
    while (!children.empty()) {
        children.front()->set_parent(std::shared_ptr<H>{});
        children.erase(children.begin());
        for (const std::string& wanted : names) {
            expect_index_agrees(*root, wanted);
        }
    }
}

TEST(Hierarchy_unique_names, CopiedParentKnowsItsChildrenNames)
{
    for (const std::size_t fillers : {std::size_t{0}, std::size_t{20}}) {
        std::shared_ptr<H> root = make("root");
        std::vector<std::shared_ptr<H>> children;
        add_fillers(root, children, fillers);
        attach(root, "Cube");
        attach(root, "Cube");
        std::shared_ptr<H> copy = std::make_shared<H>(*root);
        copy->adopt_orphan_children();
        EXPECT_EQ(copy->get_child_count(), fillers + 2);
        expect_index_agrees(*copy, "Cube");
        EXPECT_EQ(H::make_sibling_unique_name(copy.get(), "Cube", nullptr), "Cube_2");
        copy->get_children().back()->set_name("Sphere");
        expect_index_agrees(*copy, "Cube");
        EXPECT_EQ(H::make_sibling_unique_name(copy.get(), "Cube", nullptr), "Cube_1");
        EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Cube", nullptr), "Cube_2");
    }
}

// Copy assignment of an attached item and of a parent with children.
TEST(Hierarchy_unique_names, CopyAssignment)
{
    std::shared_ptr<H> root = make("root");
    std::vector<std::shared_ptr<H>> children;
    add_fillers(root, children, 20);
    std::shared_ptr<H> target = attach(root, "Target");
    std::shared_ptr<H> source = make("Source");
    add_fillers(source, children, 20);
    attach(source, "Cube");
    *target = *source; // target leaves root, takes the name Source and clones of the source's children
    expect_index_agrees(*root, "Target");
    expect_index_agrees(*root, "Source");
    EXPECT_EQ(H::make_sibling_unique_name(root.get(), "Target", nullptr), "Target");
    EXPECT_EQ(target->get_child_count(), 21u);
    expect_index_agrees(*target, "Cube");
    EXPECT_EQ(H::make_sibling_unique_name(target.get(), "Cube", nullptr), "Cube_1");
    target->set_parent(root);
    expect_index_agrees(*root, "Source");
    EXPECT_EQ(target->get_name(), "Source");
}

// A clone's children have no parent pointer until the clone is attached; reparenting one before that does not abort.
TEST(Hierarchy_unique_names, CloneChildReparentedBeforeTheCloneIsAttached)
{
    std::shared_ptr<H> root = make("root");
    std::vector<std::shared_ptr<H>> children;
    add_fillers(root, children, 20);
    std::shared_ptr<H> clone  = std::dynamic_pointer_cast<H>(root->clone());
    ASSERT_TRUE(clone);
    std::shared_ptr<H> other  = make("other");
    add_fillers(other, children, 20);
    const std::shared_ptr<H> moved = clone->get_children().front();
    moved->set_parent(other);
    EXPECT_EQ(moved->get_name(), "filler_20");
    expect_index_agrees(*other, "filler");
}

TEST(Hierarchy_unique_names, ChildOutlivingItsParentRenames)
{
    for (const std::size_t fillers : {std::size_t{0}, std::size_t{20}}) {
        std::shared_ptr<H> child = make("Cube");
        {
            std::shared_ptr<H> root = make("root");
            std::vector<std::shared_ptr<H>> children;
            add_fillers(root, children, fillers);
            child->set_parent(root);
        }
        child->set_name("Sphere");
        EXPECT_EQ(child->get_name(), "Sphere");
        std::shared_ptr<H> other = make("other");
        child->set_parent(other);
        EXPECT_EQ(H::make_sibling_unique_name(other.get(), "Sphere", nullptr), "Sphere_1");
    }
}

} // anonymous namespace
