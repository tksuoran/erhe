// Prim registration (doc/usd-compatibility-plan.md C5, U4): every `Typed`
// prim that enters a tree an `Item_host` holds reports itself to that host
// once, and reports itself out again when it leaves - whatever depth it sits
// at, and through prims that carry no transform of their own.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

// Stands in for the editor's Scene_root: keeps the list of prims it holds.
class Recording_host : public erhe::Item_host
{
public:
    auto get_host_name() const -> const char* override { return "recording_host"; }

    void register_prim(const std::shared_ptr<erhe::Typed>& prim) override
    {
        registered.push_back(prim);
        prims.push_back(prim);
    }

    void unregister_prim(const std::shared_ptr<erhe::Typed>& prim) override
    {
        unregistered.push_back(prim);
        const auto i = std::find(prims.begin(), prims.end(), prim);
        if (i != prims.end()) {
            prims.erase(i);
        }
    }

    [[nodiscard]] auto holds(const std::shared_ptr<erhe::Typed>& prim) const -> bool
    {
        return std::find(prims.begin(), prims.end(), prim) != prims.end();
    }

    std::vector<std::shared_ptr<erhe::Typed>> prims;
    std::vector<std::shared_ptr<erhe::Typed>> registered;
    std::vector<std::shared_ptr<erhe::Typed>> unregistered;
};

// A typed prim of no particular class, as a content-library resource is.
class Resource_prim : public erhe::Item<erhe::Item_base, erhe::Typed, Resource_prim>
{
public:
    explicit Resource_prim(const std::string_view name) : Item{name} {}
    explicit Resource_prim(const Resource_prim& other) = default;
    static constexpr std::string_view static_type_name{"Resource_prim"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Typed::get_static_type() | (uint64_t{1} << 56);
    }
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Resource_prim"; }
};

// The prim a host's tree hangs off; the host is its own, as a scene's root
// node reports the scene.
class Hosted_root : public erhe::Item<erhe::Item_base, erhe::Typed, Hosted_root>
{
public:
    explicit Hosted_root(const std::string_view name) : Item{name} {}
    explicit Hosted_root(const Hosted_root& other) = default;
    static constexpr std::string_view static_type_name{"Hosted_root"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Typed::get_static_type() | (uint64_t{1} << 55);
    }
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Hosted_root"; }
};

[[nodiscard]] auto make_hosted_root(Recording_host& host) -> std::shared_ptr<Hosted_root>
{
    std::shared_ptr<Hosted_root> root = std::make_shared<Hosted_root>("Root");
    root->set_item_host(&host);
    return root;
}

TEST(PrimRegistration, PrimUnderScopeUnderRootRegistersOnce)
{
    Recording_host host;
    const std::shared_ptr<Hosted_root>  root     = make_hosted_root(host);
    const std::shared_ptr<erhe::Scope>  scope    = std::make_shared<erhe::Scope>("Materials");
    const std::shared_ptr<Resource_prim> resource = std::make_shared<Resource_prim>("Copper");

    // The resource is attached to the scope before the scope reaches the
    // tree, so the host learns of both in one carry-down.
    resource->set_parent(scope);
    EXPECT_EQ(host.registered.size(), 0u);

    scope->set_parent(root);

    EXPECT_TRUE(host.holds(scope));
    EXPECT_TRUE(host.holds(resource));
    EXPECT_EQ(std::count(host.registered.begin(), host.registered.end(), std::static_pointer_cast<erhe::Typed>(resource)), 1);
    EXPECT_EQ(resource->get_item_host(), &host);
    EXPECT_EQ(scope->get_item_host(), &host);
}

TEST(PrimRegistration, PrimAddedToHostedScopeRegisters)
{
    Recording_host host;
    const std::shared_ptr<Hosted_root>  root  = make_hosted_root(host);
    const std::shared_ptr<erhe::Scope>  scope = std::make_shared<erhe::Scope>("Materials");
    scope->set_parent(root);
    host.registered.clear();

    const std::shared_ptr<Resource_prim> resource = std::make_shared<Resource_prim>("Copper");
    resource->set_parent(scope);

    EXPECT_TRUE(host.holds(resource));
    EXPECT_EQ(host.registered.size(), 1u);
    EXPECT_EQ(host.registered.front(), std::static_pointer_cast<erhe::Typed>(resource));
}

TEST(PrimRegistration, DetachUnregistersSubtree)
{
    Recording_host host;
    const std::shared_ptr<Hosted_root>   root     = make_hosted_root(host);
    const std::shared_ptr<erhe::Scope>   scope    = std::make_shared<erhe::Scope>("Materials");
    const std::shared_ptr<erhe::Scope>   folder   = std::make_shared<erhe::Scope>("Metals");
    const std::shared_ptr<Resource_prim> resource = std::make_shared<Resource_prim>("Copper");
    scope->set_parent(root);
    folder->set_parent(scope);
    resource->set_parent(folder);
    EXPECT_TRUE(host.holds(resource));

    // Detaches the scope with its subtree; Hierarchy::remove() would splice
    // the scope out and leave its children under the hosted root.
    scope->set_parent(std::shared_ptr<erhe::Hierarchy>{});

    EXPECT_FALSE(host.holds(scope));
    EXPECT_FALSE(host.holds(folder));
    EXPECT_FALSE(host.holds(resource));
    EXPECT_EQ(resource->get_item_host(), nullptr);
    EXPECT_EQ(std::count(host.unregistered.begin(), host.unregistered.end(), std::static_pointer_cast<erhe::Typed>(resource)), 1);
}

TEST(PrimRegistration, MoveBetweenHostsUnregistersThenRegisters)
{
    Recording_host host_a;
    Recording_host host_b;
    const std::shared_ptr<Hosted_root>   root_a   = make_hosted_root(host_a);
    const std::shared_ptr<Hosted_root>   root_b   = make_hosted_root(host_b);
    const std::shared_ptr<Resource_prim> resource = std::make_shared<Resource_prim>("Copper");

    resource->set_parent(root_a);
    EXPECT_TRUE(host_a.holds(resource));

    resource->set_parent(root_b);

    EXPECT_FALSE(host_a.holds(resource));
    EXPECT_TRUE (host_b.holds(resource));
    EXPECT_EQ(resource->get_item_host(), &host_b);
}

// A content-library resource kind: a typed prim of a class the editor's
// Content_library indexes, placed under a kind Scope of the hosted tree
// (doc/usd-compatibility-plan.md U4).
TEST(PrimRegistration, ResourceKindUnderKindScopeRegistersWithItsClass)
{
    Recording_host host;
    const std::shared_ptr<Hosted_root>   root      = make_hosted_root(host);
    const std::shared_ptr<erhe::Scope>   materials = std::make_shared<erhe::Scope>("Materials");
    const std::shared_ptr<erhe::Scope>   metals    = std::make_shared<erhe::Scope>("Metals");
    const std::shared_ptr<Resource_prim> copper    = std::make_shared<Resource_prim>("Copper");
    materials->set_parent(root);
    metals->set_parent(materials);
    copper->set_parent(metals);

    ASSERT_EQ(host.prims.size(), 3u);
    EXPECT_TRUE(host.holds(copper));
    EXPECT_EQ(host.registered.back()->get_type_name(), Resource_prim::static_type_name);
    EXPECT_EQ(copper->get_path(), "Materials/Metals/Copper");

    // A move to another folder scope of the same host keeps the host, so the
    // host is not told: a move is not a removal followed by an addition, and
    // the index it keeps stays as it is.
    copper->set_parent(materials);
    EXPECT_TRUE(host.holds(copper));
    EXPECT_EQ(std::count(host.unregistered.begin(), host.unregistered.end(), std::static_pointer_cast<erhe::Typed>(copper)), 0);
    EXPECT_EQ(std::count(host.registered.begin(),   host.registered.end(),   std::static_pointer_cast<erhe::Typed>(copper)), 1);
    EXPECT_EQ(copper->get_path(), "Materials/Copper");
}

TEST(PrimRegistration, UnhostedTreeRegistersNothing)
{
    Recording_host host;
    const std::shared_ptr<erhe::Scope>   scope    = std::make_shared<erhe::Scope>("Materials");
    const std::shared_ptr<Resource_prim> resource = std::make_shared<Resource_prim>("Copper");

    resource->set_parent(scope);

    EXPECT_EQ(host.registered.size(), 0u);
    EXPECT_EQ(resource->get_item_host(), nullptr);
}

} // anonymous namespace
