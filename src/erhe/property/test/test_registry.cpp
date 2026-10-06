#include "test_object.hpp"

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

const Property<float>       reg_float  = Property<float>::register_property("reg_float", type_a(), Property_metadata{.default_value = 2.5f});
const Property<glm::vec3>   reg_vec3   = Property<glm::vec3>::register_property("reg_vec3", type_a());
const Property<std::string> reg_string = Property<std::string>::register_property("reg_string", type_b(), Property_metadata{.default_value = std::string{"hello"}});
const Property<int>         reg_attached = Property<int>::register_attached("reg_attached", type_c(), type_a());

} // anonymous namespace

TEST(Property_registry, registered_properties_have_expected_records)
{
    EXPECT_EQ(reg_float.get().get_name(), "reg_float");
    EXPECT_EQ(reg_float.get().get_type(), Property_type::floating);
    EXPECT_EQ(reg_float.get().get_owner_type(), type_a());
    EXPECT_FALSE(reg_float.get().is_read_only());
    EXPECT_FALSE(reg_float.get().is_attached());
    EXPECT_TRUE(reg_attached.get().is_attached());
    EXPECT_EQ(reg_vec3.get().get_type(), Property_type::vec3);
    EXPECT_EQ(reg_string.get().get_type(), Property_type::string);
}

TEST(Property_registry, indices_are_distinct_and_resolve_back)
{
    Property_registry& registry = Property_registry::get();
    EXPECT_NE(reg_float.get().get_index(), reg_vec3.get().get_index());
    EXPECT_EQ(&registry.get(reg_float.get().get_index()), reg_float.get_ptr());
    EXPECT_EQ(&registry.get(reg_vec3.get().get_index()), reg_vec3.get_ptr());
    EXPECT_GE(registry.get_count(), std::size_t{4});
}

TEST(Property_registry, owner_type_count_covers_every_allocated_id)
{
    Property_registry& registry = Property_registry::get();
    const std::size_t count = registry.get_owner_type_count();
    EXPECT_LT(static_cast<std::size_t>(type_a()), count);
    EXPECT_LT(static_cast<std::size_t>(type_b()), count);
    EXPECT_LT(static_cast<std::size_t>(type_c()), count);
    EXPECT_EQ(registry.get_owner_name(root_owner_type), "root");
    EXPECT_EQ(registry.get_owner_name(type_a()), "type_a");
    // The walk tooling makes: every id below the count names an owner type.
    bool found_type_b = false;
    for (std::size_t id = 0; id < count; ++id) {
        if (registry.get_owner_name(static_cast<Owner_type>(id)) == "type_b") {
            found_type_b = true;
        }
    }
    EXPECT_TRUE(found_type_b);
}

TEST(Property_registry, find_by_owner_and_name)
{
    Property_registry& registry = Property_registry::get();
    EXPECT_EQ(registry.find(type_a(), "reg_float"), reg_float.get_ptr());
    EXPECT_EQ(registry.find(type_b(), "reg_string"), reg_string.get_ptr());
    EXPECT_EQ(registry.find(type_b(), "reg_float"), nullptr);
    EXPECT_EQ(registry.find(type_a(), "no_such_property"), nullptr);
}

TEST(Property_registry, default_values)
{
    EXPECT_EQ(std::get<float>(reg_float.get().get_default_value(type_a())), 2.5f);
    EXPECT_EQ(std::get<glm::vec3>(reg_vec3.get().get_default_value(type_a())), glm::vec3{0.0f});
    EXPECT_EQ(std::get<std::string>(reg_string.get().get_default_value(type_b())), "hello");
}

TEST(Property_registry, for_each_property_of_object_lists_non_attached_owners_only)
{
    std::vector<std::string> names_a;
    Property_registry::get().for_each_property_of_object(type_a(), [&](const Dependency_property& p) { names_a.emplace_back(p.get_name()); });
    EXPECT_NE(std::find(names_a.begin(), names_a.end(), "reg_float"), names_a.end());
    EXPECT_NE(std::find(names_a.begin(), names_a.end(), "reg_vec3"), names_a.end());
    EXPECT_EQ(std::find(names_a.begin(), names_a.end(), "reg_string"), names_a.end());

    std::vector<std::string> names_c;
    Property_registry::get().for_each_property_of_object(type_c(), [&](const Dependency_property& p) { names_c.emplace_back(p.get_name()); });
    EXPECT_EQ(std::find(names_c.begin(), names_c.end(), "reg_attached"), names_c.end());
}

TEST(Property_registry, override_metadata_per_owner_type)
{
    const Property<float> p = Property<float>::register_property("reg_override", type_a(), Property_metadata{.default_value = 1.0f});
    Property_metadata override_metadata{};
    override_metadata.default_value = 7.0f;
    override_metadata.inherits      = true;
    override_metadata.flags         = Property_flags::affects_transform;
    override_metadata.ui.min        = -1.0f;
    const_cast<Dependency_property&>(p.get()).override_metadata(type_b(), override_metadata);

    EXPECT_EQ(std::get<float>(p.get().get_default_value(type_a())), 1.0f);
    EXPECT_EQ(std::get<float>(p.get().get_default_value(type_b())), 7.0f);
    EXPECT_FALSE(p.get().get_metadata(type_a()).inherits);
    EXPECT_TRUE (p.get().get_metadata(type_b()).inherits);
    EXPECT_EQ(p.get().get_metadata(type_b()).flags, Property_flags::affects_transform);
    EXPECT_EQ(p.get().get_metadata(type_b()).ui.min, -1.0f);
    // An object of type_b() sees the override; type_a() the default.
    Test_object a{type_a()};
    Test_object b{type_b()};
    EXPECT_EQ(a.get_value(p), 1.0f);
    EXPECT_EQ(b.get_value(p), 7.0f);
    // The override's missing fields keep the default metadata's default value.
    const_cast<Dependency_property&>(p.get()).override_metadata(type_c(), Property_metadata{.inherits = true});
    EXPECT_EQ(std::get<float>(p.get().get_default_value(type_c())), 1.0f);
}

TEST(Property_registry, add_owner_makes_property_findable_and_listed_for_new_owner)
{
    const Property<int> p = Property<int>::register_property("reg_add_owner", type_a(), Property_metadata{.default_value = 3});
    const_cast<Dependency_property&>(p.get()).add_owner(type_b(), Property_metadata{.default_value = 4});
    EXPECT_EQ(Property_registry::get().find(type_b(), "reg_add_owner"), p.get_ptr());
    std::vector<std::string> names_b;
    Property_registry::get().for_each_property_of_object(type_b(), [&](const Dependency_property& q) { names_b.emplace_back(q.get_name()); });
    EXPECT_NE(std::find(names_b.begin(), names_b.end(), "reg_add_owner"), names_b.end());
    Test_object b{type_b()};
    EXPECT_EQ(b.get_value(p), 4);
}

TEST(Property_registry, owner_type_chain_lists_finds_and_shadows)
{
    Property_registry& registry = Property_registry::get();
    const Owner_type parent = type_a();
    const Owner_type child  = type_a_child();
    EXPECT_EQ(get_owner_type_parent(child), parent);
    EXPECT_EQ(get_owner_type_parent(parent), root_owner_type);
    EXPECT_EQ(get_owner_type_name(child), "type_a_child");
    EXPECT_TRUE (is_owner_type_or_descendant(child, parent));
    EXPECT_TRUE (is_owner_type_or_descendant(child, root_owner_type));
    EXPECT_FALSE(is_owner_type_or_descendant(parent, child));

    // The same name registers on the parent and on the child; the child's
    // registration shadows the parent's for child objects only.
    const Property<float> chain_parent = Property<float>::register_property("reg_chain_float", parent, Property_metadata{.default_value = 0.5f});
    const Property<float> chain_child  = Property<float>::register_property("reg_chain_float", child,  Property_metadata{.default_value = 1.0f});
    const Property<int>   child_only   = Property<int>::register_property("reg_child_only", child);
    const Property<int>   root_level   = Property<int>::register_property("reg_root_level", root_owner_type, Property_metadata{.default_value = 3});

    EXPECT_EQ(chain_parent.get().get_owner_type(), parent);
    EXPECT_EQ(chain_child.get().get_owner_type(), child);

    // find() is exact; find_for_object() walks the chain, nearest first.
    EXPECT_EQ(registry.find(parent, "reg_chain_float"), chain_parent.get_ptr());
    EXPECT_EQ(registry.find(child,  "reg_chain_float"), chain_child.get_ptr());
    EXPECT_EQ(registry.find(parent, "reg_child_only"), nullptr);
    EXPECT_EQ(registry.find_for_object(child,  "reg_chain_float"), chain_child.get_ptr());
    EXPECT_EQ(registry.find_for_object(parent, "reg_chain_float"), chain_parent.get_ptr());
    EXPECT_EQ(registry.find_for_object(child,  "reg_float"),       reg_float.get_ptr());
    EXPECT_EQ(registry.find_for_object(child,  "reg_root_level"),  root_level.get_ptr());
    EXPECT_EQ(registry.find_for_object(parent, "reg_child_only"),  nullptr);
    EXPECT_EQ(registry.find_for_object(type_b(), "reg_chain_float"), nullptr);

    // Listing for the child: root level first, then the parent's, then its
    // own; the shadowed parent registration is not listed.
    std::vector<const Dependency_property*> listed_child;
    registry.for_each_property_of_object(child, [&](const Dependency_property& p) { listed_child.push_back(&p); });
    const auto position = [&listed_child](const Dependency_property* p) { return std::find(listed_child.begin(), listed_child.end(), p) - listed_child.begin(); };
    EXPECT_NE(std::find(listed_child.begin(), listed_child.end(), chain_child.get_ptr()), listed_child.end());
    EXPECT_NE(std::find(listed_child.begin(), listed_child.end(), child_only.get_ptr()),  listed_child.end());
    EXPECT_NE(std::find(listed_child.begin(), listed_child.end(), reg_float.get_ptr()),   listed_child.end());
    EXPECT_NE(std::find(listed_child.begin(), listed_child.end(), root_level.get_ptr()),  listed_child.end());
    EXPECT_EQ(std::find(listed_child.begin(), listed_child.end(), chain_parent.get_ptr()), listed_child.end());
    EXPECT_LT(position(root_level.get_ptr()), position(reg_float.get_ptr()));
    EXPECT_LT(position(reg_float.get_ptr()),  position(chain_child.get_ptr()));

    std::vector<const Dependency_property*> listed_parent;
    registry.for_each_property_of_object(parent, [&](const Dependency_property& p) { listed_parent.push_back(&p); });
    EXPECT_NE(std::find(listed_parent.begin(), listed_parent.end(), chain_parent.get_ptr()), listed_parent.end());
    EXPECT_EQ(std::find(listed_parent.begin(), listed_parent.end(), chain_child.get_ptr()),  listed_parent.end());
    EXPECT_EQ(std::find(listed_parent.begin(), listed_parent.end(), child_only.get_ptr()),   listed_parent.end());

    // Listing for the root yields root-level registrations only.
    std::vector<const Dependency_property*> listed_root;
    registry.for_each_property_of_object(root_owner_type, [&](const Dependency_property& p) { listed_root.push_back(&p); });
    EXPECT_NE(std::find(listed_root.begin(), listed_root.end(), root_level.get_ptr()), listed_root.end());
    EXPECT_EQ(std::find(listed_root.begin(), listed_root.end(), reg_float.get_ptr()),  listed_root.end());

    // An object's id selects its view of the registry.
    Test_object o_parent{parent};
    Test_object o_child{child};
    EXPECT_EQ(o_parent.get_value(chain_parent), 0.5f);
    EXPECT_EQ(o_child.get_value(chain_child), 1.0f);
    EXPECT_EQ(o_child.get_value(root_level), 3);
}

TEST(Property_registry, override_resolves_to_nearest_ancestor)
{
    const Property<float> p = Property<float>::register_property("reg_chain_override", root_owner_type, Property_metadata{.default_value = 1.0f});
    // An override on the parent applies to the child; an override on the
    // child wins over it.
    const_cast<Dependency_property&>(p.get()).override_metadata(type_a(), Property_metadata{.default_value = 2.0f});
    EXPECT_EQ(std::get<float>(p.get().get_default_value(type_a())), 2.0f);
    EXPECT_EQ(std::get<float>(p.get().get_default_value(type_a_child())), 2.0f);
    EXPECT_EQ(std::get<float>(p.get().get_default_value(type_b())), 1.0f);
    const_cast<Dependency_property&>(p.get()).override_metadata(type_a_child(), Property_metadata{.default_value = 3.0f});
    EXPECT_EQ(std::get<float>(p.get().get_default_value(type_a())), 2.0f);
    EXPECT_EQ(std::get<float>(p.get().get_default_value(type_a_child())), 3.0f);
    Test_object child{type_a_child()};
    EXPECT_EQ(child.get_value(p), 3.0f);
}

TEST(Property_registry, add_owner_on_the_chain_is_listed_once)
{
    const Property<int> p = Property<int>::register_property("reg_multi_owner", type_a(), Property_metadata{.default_value = 1});
    const_cast<Dependency_property&>(p.get()).add_owner(type_a_child(), Property_metadata{.default_value = 2});
    std::vector<const Dependency_property*> listed;
    Property_registry::get().for_each_property_of_object(type_a_child(), [&](const Dependency_property& q) { listed.push_back(&q); });
    EXPECT_EQ(std::count(listed.begin(), listed.end(), p.get_ptr()), 1);
    Test_object child{type_a_child()};
    EXPECT_EQ(child.get_value(p), 2);
}

TEST(Property_registry, read_only_key)
{
    const Property_key<int> key = Property_key<int>::register_read_only("reg_read_only", type_a(), Property_metadata{.default_value = 10});
    const Property<int> p = key.get_property();
    EXPECT_TRUE(p.get().is_read_only());
    Test_object o;
    o.set_value(p.get(), Property_value{20}); // untyped write: rejected
    EXPECT_EQ(o.get_value(p), 10);
    o.set_value(key, 30);                     // key write: accepted
    EXPECT_EQ(o.get_value(p), 30);
    o.clear_value(p);                         // clear without key: rejected
    EXPECT_EQ(o.get_value(p), 30);
    o.clear_value(key);
    EXPECT_EQ(o.get_value(p), 10);
}

TEST(Property_registry, attached_property_resolves_by_qualified_name_on_its_holder_type)
{
    Property_registry& registry = Property_registry::get();
    // Attached: registered by type_c, held by objects of type_a and its
    // descendants (R7); a qualified name resolves on those only.
    EXPECT_EQ(registry.qualified_name(reg_attached.get()), "type_c.reg_attached");
    EXPECT_EQ(registry.qualified_name(reg_float.get()), "reg_float");
    EXPECT_EQ(registry.find_owner_type("type_c"), std::optional<Owner_type>{type_c()});
    EXPECT_FALSE(registry.find_owner_type("no_such_type").has_value());
    EXPECT_EQ(reg_attached.get().get_holder_type(), type_a());
    EXPECT_TRUE (reg_attached.get().applies_to(type_a()));
    EXPECT_TRUE (reg_attached.get().applies_to(type_a_child()));
    EXPECT_FALSE(reg_attached.get().applies_to(type_b()));
    EXPECT_FALSE(reg_attached.get().applies_to(type_c()));
    EXPECT_FALSE(reg_float.get().applies_to(type_a()));
    EXPECT_EQ(registry.find_for_object(type_a(),       "type_c.reg_attached"), reg_attached.get_ptr());
    EXPECT_EQ(registry.find_for_object(type_a_child(), "type_c.reg_attached"), reg_attached.get_ptr());
    EXPECT_EQ(registry.find_for_object(type_c(),       "type_c.reg_attached"), nullptr);
    EXPECT_EQ(registry.find_for_object(type_b(),       "type_c.reg_attached"), nullptr);
    std::vector<const Dependency_property*> held_by_a;
    registry.for_each_attached_property_of(type_a(), [&held_by_a](const Dependency_property& property) { held_by_a.push_back(&property); });
    EXPECT_NE(std::find(held_by_a.begin(), held_by_a.end(), reg_attached.get_ptr()), held_by_a.end());
    std::vector<const Dependency_property*> held_by_b;
    registry.for_each_attached_property_of(type_b(), [&held_by_b](const Dependency_property& property) { held_by_b.push_back(&property); });
    EXPECT_EQ(std::find(held_by_b.begin(), held_by_b.end(), reg_attached.get_ptr()), held_by_b.end());
    // The chain walk never returns an attached property, and a qualified
    // name never resolves a non-attached one.
    EXPECT_EQ(registry.find_for_object(type_c(), "reg_attached"), nullptr);
    EXPECT_EQ(registry.find_for_object(type_a(), "type_a.reg_float"), nullptr);
    EXPECT_EQ(registry.find_for_object(type_a(), "no_such_type.reg_attached"), nullptr);

    std::vector<const Dependency_property*> attached;
    registry.for_each_attached_property([&](const Dependency_property& p) { attached.push_back(&p); });
    EXPECT_NE(std::find(attached.begin(), attached.end(), reg_attached.get_ptr()), attached.end());
    EXPECT_EQ(std::find(attached.begin(), attached.end(), reg_float.get_ptr()), attached.end());

    Test_object a{type_a()};
    a.set_value(reg_attached, 7);
    EXPECT_EQ(a.get_value(reg_attached), 7);
    EXPECT_EQ(a.get_value_source(reg_attached), Value_source::local);
    std::vector<const Dependency_property*> local;
    a.for_each_local_value([&](const Dependency_property& p, const Property_value&) { local.push_back(&p); });
    EXPECT_NE(std::find(local.begin(), local.end(), reg_attached.get_ptr()), local.end());
}
