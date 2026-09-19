#include "test_object.hpp"
#include "erhe_property/expression.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

inline auto type_ref() -> Owner_type { static const Owner_type id = allocate_owner_type(root_owner_type, "type_ref"); return id; }

// A referenceable object: it has a name (the reference path), is owned by a
// shared_ptr, and resolves names through a registry the test fills.
class Named_object : public Test_object
{
public:
    explicit Named_object(std::string name) : Test_object{type_ref()}, m_name{std::move(name)} {}

    auto get_reference_path() const -> std::string override { return m_name; }
    auto get_shared_reference() const -> std::shared_ptr<Dependency_object> override { return m_self.lock(); }
    auto resolve_expression_object(const std::string_view path) const -> Dependency_object* override
    {
        if (path.empty()) {
            return const_cast<Named_object*>(this);
        }
        for (const std::shared_ptr<Named_object>& candidate : *m_registry) {
            if (candidate->m_name == path) {
                return candidate.get();
            }
        }
        return nullptr;
    }

    static auto make(std::vector<std::shared_ptr<Named_object>>& registry, std::string name) -> std::shared_ptr<Named_object>
    {
        std::shared_ptr<Named_object> object = std::make_shared<Named_object>(std::move(name));
        object->m_self     = object;
        object->m_registry = &registry;
        registry.push_back(object);
        return object;
    }

    // A member-backed reference (register_member over a shared_ptr member)
    std::shared_ptr<Named_object> partner{};
    // The same, over a weak_ptr member (P1)
    std::weak_ptr<Named_object>   observer{};
    float                         weight{0.0f};
    int                           after_set_calls{0};

private:
    std::string                                 m_name;
    std::weak_ptr<Named_object>                 m_self;
    std::vector<std::shared_ptr<Named_object>>* m_registry{nullptr};
};

// Validate: only a Named_object may be referenced (rejects a plain Test_object).
const Property<Object_reference> target_property = Property<Object_reference>::register_property(
    "target", type_ref(), Property_metadata{},
    [](const Property_value& value) -> bool {
        const Object_reference& reference = std::get<Object_reference>(value);
        return (!reference.object) || (dynamic_cast<const Named_object*>(reference.object.get()) != nullptr);
    }
);

const Property<Object_reference> partner_property = Property<Object_reference>::register_member(
    "partner", type_ref(), &Named_object::partner, Property_metadata{},
    [](Named_object& object) { ++object.after_set_calls; }
);

const Property<float> weight_property = Property<float>::register_member<Named_object, float>(
    "weight", type_ref(), [](auto& object) -> auto& { return object.weight; }, Property_metadata{.default_value = 2.5f},
    [](Named_object& object) { ++object.after_set_calls; }
);

const Property<float> scalar_property = Property<float>::register_property("ref_scalar", type_ref());

// The weak kind (P1): the same registration, validation and member backing.
const Property<Weak_object_reference> weak_target_property = Property<Weak_object_reference>::register_property(
    "weak_target", type_ref(), Property_metadata{},
    [](const Property_value& value) -> bool {
        const std::shared_ptr<Dependency_object> object = get_referenced_object(value);
        return (!object) || (dynamic_cast<const Named_object*>(object.get()) != nullptr);
    }
);

const Property<Weak_object_reference> observer_property = Property<Weak_object_reference>::register_member(
    "observer", type_ref(), &Named_object::observer, Property_metadata{},
    [](Named_object& object) { ++object.after_set_calls; }
);

// Drops the object from the registry that keeps it alive, so the last
// std::shared_ptr the caller holds is the only one left.
void forget(std::vector<std::shared_ptr<Named_object>>& registry, const std::shared_ptr<Named_object>& object)
{
    std::erase(registry, object);
}

} // anonymous namespace

TEST(Object_reference_property, default_is_null_and_store_holds_the_pointer)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a = Named_object::make(registry, "a");
    std::shared_ptr<Named_object> b = Named_object::make(registry, "b");

    EXPECT_FALSE(a->get_value(target_property).object);
    EXPECT_EQ(a->get_value_source(target_property.get()), Value_source::default_value);

    a->set_value(target_property, Object_reference{b});
    EXPECT_EQ(a->get_value(target_property).object.get(), b.get());
    EXPECT_EQ(a->get_value_source(target_property.get()), Value_source::local);
    EXPECT_EQ(a->change_count("target"), std::size_t{1});
    a->changes.clear(); // the recorded new_value holds b too
    EXPECT_EQ(b.use_count(), 3); // registry, local b, the entry

    // Same pointee: no change.
    a->set_value(target_property, Object_reference{b});
    EXPECT_EQ(a->change_count("target"), std::size_t{0});

    a->clear_value(target_property);
    EXPECT_FALSE(a->get_value(target_property).object);
    a->changes.clear(); // the recorded old_value
    EXPECT_EQ(b.use_count(), 2);
}

TEST(Object_reference_property, validate_rejects_a_wrong_class)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a     = Named_object::make(registry, "a");
    std::shared_ptr<Test_object>  plain = std::make_shared<Test_object>();

    a->set_value(target_property, Object_reference{plain});
    EXPECT_FALSE(a->has_local_value(target_property.get()));
    EXPECT_FALSE(a->get_value(target_property).object);
}

TEST(Object_reference_property, string_form_and_context_parse)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a = Named_object::make(registry, "a");
    std::shared_ptr<Named_object> b = Named_object::make(registry, "b");

    EXPECT_EQ(to_string(Property_value{Object_reference{}}), "");
    EXPECT_EQ(to_string(Property_value{Object_reference{b}}), "b");

    // The context-free parse cannot resolve a name.
    EXPECT_FALSE(parse_value(Property_type::object, "b").has_value());

    const std::optional<Property_value> found = parse_value(*a, target_property.get(), " b ");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(std::get<Object_reference>(found.value()).object.get(), b.get());

    const std::optional<Property_value> cleared = parse_value(*a, target_property.get(), "");
    ASSERT_TRUE(cleared.has_value());
    EXPECT_FALSE(std::get<Object_reference>(cleared.value()).object);

    EXPECT_FALSE(parse_value(*a, target_property.get(), "nobody").has_value());

    // An object that is not shareable cannot be referenced through a name.
    Named_object stack_object{"stack"};
    registry.push_back(std::shared_ptr<Named_object>{&stack_object, [](Named_object*) {}});
    EXPECT_FALSE(parse_value(*a, target_property.get(), "stack").has_value());
    registry.pop_back();

    // Non-object types delegate to the plain parser.
    const std::optional<Property_value> scalar = parse_value(*a, scalar_property.get(), "1.5");
    ASSERT_TRUE(scalar.has_value());
    EXPECT_EQ(std::get<float>(scalar.value()), 1.5f);
}

TEST(Object_reference_property, copy_shares_the_pointee_and_property_set_compares_identity)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a = Named_object::make(registry, "a");
    std::shared_ptr<Named_object> b = Named_object::make(registry, "b");
    std::shared_ptr<Named_object> c = Named_object::make(registry, "c");

    a->set_value(target_property, Object_reference{b});
    Named_object copy{*a};
    EXPECT_EQ(copy.get_value(target_property).object.get(), b.get());

    const Property_set bag_a = Property_set::read_local_values(*a);
    const Property_set bag_c = Property_set::read_local_values(copy);
    EXPECT_TRUE(bag_a == bag_c);

    copy.set_value(target_property, Object_reference{c});
    const Property_set bag_d = Property_set::read_local_values(copy);
    EXPECT_FALSE(bag_a == bag_d);
    EXPECT_EQ(Property_set::diff(bag_a, bag_d).size(), std::size_t{1});
}

TEST(Object_reference_property, expressions_exclude_object_properties)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a = Named_object::make(registry, "a");
    std::shared_ptr<Named_object> b = Named_object::make(registry, "b");
    a->set_value(target_property, Object_reference{b});

    EXPECT_FALSE(a->set_expression(target_property.get(), "1"));
    EXPECT_EQ(a->get_value(target_property).object.get(), b.get());

    EXPECT_TRUE(a->set_expression(scalar_property.get(), "{target}"));
    static_cast<void>(a->get_value(scalar_property));
    EXPECT_NE(a->get_expression_error(scalar_property.get()).find("object reference"), std::string_view::npos);
}

TEST(Member_backed_property, shared_ptr_member_round_trips_through_traits)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a = Named_object::make(registry, "a");
    std::shared_ptr<Named_object> b = Named_object::make(registry, "b");

    // The member is the value.
    a->partner = b;
    EXPECT_EQ(a->get_value(partner_property).object.get(), b.get());
    EXPECT_EQ(a->get_value_source(partner_property.get()), Value_source::local);

    // set writes the member and runs after_set.
    a->set_value(partner_property, Object_reference{});
    EXPECT_FALSE(a->partner);
    EXPECT_EQ(a->after_set_calls, 1);
    EXPECT_EQ(a->change_count("partner"), std::size_t{1});

    a->set_value(partner_property, Object_reference{b});
    EXPECT_EQ(a->partner.get(), b.get());
    EXPECT_EQ(a->after_set_calls, 2);

    // Unchanged: neither the write nor after_set.
    a->set_value(partner_property, Object_reference{b});
    EXPECT_EQ(a->after_set_calls, 2);

    // The traits' validate rejects a pointee that is not a Named_object.
    std::shared_ptr<Test_object> plain = std::make_shared<Test_object>();
    a->set_value(partner_property, Object_reference{plain});
    EXPECT_EQ(a->partner.get(), b.get());
    EXPECT_EQ(a->after_set_calls, 2);

    // Clear writes the default (null).
    a->clear_value(partner_property);
    EXPECT_FALSE(a->partner);
    EXPECT_EQ(a->after_set_calls, 3);
}

TEST(Member_backed_property, scalar_member_through_accessor)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a = Named_object::make(registry, "a");

    EXPECT_EQ(a->get_value(weight_property), 0.0f); // the member, not the default
    a->set_value(weight_property, 4.0f);
    EXPECT_EQ(a->weight, 4.0f);
    EXPECT_EQ(a->after_set_calls, 1);
    a->clear_value(weight_property);
    EXPECT_EQ(a->weight, 2.5f);
    EXPECT_EQ(a->after_set_calls, 2);
}

TEST(Weak_object_reference_property, default_is_null_and_the_store_does_not_own_the_target)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a = Named_object::make(registry, "a");
    std::shared_ptr<Named_object> b = Named_object::make(registry, "b");

    EXPECT_FALSE(a->get_value(weak_target_property).object.lock());
    EXPECT_EQ(a->get_value_source(weak_target_property.get()), Value_source::default_value);

    a->set_value(weak_target_property, Weak_object_reference{b});
    EXPECT_EQ(a->get_value(weak_target_property).object.lock().get(), b.get());
    EXPECT_EQ(a->get_value_source(weak_target_property.get()), Value_source::local);
    EXPECT_EQ(a->change_count("weak_target"), std::size_t{1});
    a->changes.clear();
    EXPECT_EQ(b.use_count(), 2); // registry and local b: the entry owns nothing

    // Same pointee: no change.
    a->set_value(weak_target_property, Weak_object_reference{b});
    EXPECT_EQ(a->change_count("weak_target"), std::size_t{0});

    a->clear_value(weak_target_property);
    EXPECT_FALSE(a->get_value(weak_target_property).object.lock());
}

TEST(Weak_object_reference_property, an_expired_target_reads_as_an_empty_reference)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a = Named_object::make(registry, "a");
    std::shared_ptr<Named_object> b = Named_object::make(registry, "b");

    a->set_value(weak_target_property, Weak_object_reference{b});
    EXPECT_EQ(get_referenced_object(a->get_value(weak_target_property)).get(), b.get());

    forget(registry, b);
    b.reset();

    EXPECT_FALSE(get_referenced_object(a->get_value(weak_target_property)));
    EXPECT_EQ(to_string(a->get_value(weak_target_property)), "");
    // The target expired; the opinion did not.
    EXPECT_EQ(a->get_value_source(weak_target_property.get()), Value_source::local);
}

TEST(Weak_object_reference_property, equality_names_the_control_block)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> b = Named_object::make(registry, "b");
    std::shared_ptr<Named_object> c = Named_object::make(registry, "c");

    EXPECT_TRUE (Weak_object_reference{b} == Weak_object_reference{b});
    EXPECT_FALSE(Weak_object_reference{b} == Weak_object_reference{c});
    EXPECT_TRUE (Weak_object_reference{}  == Weak_object_reference{});
    EXPECT_FALSE(Weak_object_reference{b} == Weak_object_reference{});

    // The variant compares through that operator, and the two kinds are
    // different alternatives.
    EXPECT_TRUE (Property_value{Weak_object_reference{b}} == Property_value{Weak_object_reference{b}});
    EXPECT_FALSE(Property_value{Weak_object_reference{b}} == Property_value{Object_reference{b}});

    // An expired reference still names its own control block, not an empty one.
    const Weak_object_reference expiring{c};
    forget(registry, c);
    c.reset();
    EXPECT_FALSE(expiring == Weak_object_reference{});
    EXPECT_TRUE (expiring == expiring);
}

TEST(Weak_object_reference_property, string_form_and_context_parse)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a = Named_object::make(registry, "a");
    std::shared_ptr<Named_object> b = Named_object::make(registry, "b");

    EXPECT_EQ(to_string(Property_value{Weak_object_reference{}}), "");
    EXPECT_EQ(to_string(Property_value{Weak_object_reference{b}}), "b");

    // The context-free parse cannot resolve a name.
    EXPECT_FALSE(parse_value(Property_type::weak_object, "b").has_value());

    const std::optional<Property_value> found = parse_value(*a, weak_target_property.get(), " b ");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(type_of(found.value()), Property_type::weak_object);
    EXPECT_EQ(get_referenced_object(found.value()).get(), b.get());

    const std::optional<Property_value> cleared = parse_value(*a, weak_target_property.get(), "");
    ASSERT_TRUE(cleared.has_value());
    EXPECT_EQ(type_of(cleared.value()), Property_type::weak_object);
    EXPECT_FALSE(get_referenced_object(cleared.value()));

    EXPECT_FALSE(parse_value(*a, weak_target_property.get(), "nobody").has_value());

    // Round trip through the text form.
    a->set_value(weak_target_property, Weak_object_reference{b});
    const std::optional<Property_value> reparsed = parse_value(*a, weak_target_property.get(), to_string(a->get_value(weak_target_property)));
    ASSERT_TRUE(reparsed.has_value());
    EXPECT_EQ(get_referenced_object(reparsed.value()).get(), b.get());
}

TEST(Weak_object_reference_property, validate_rejects_a_wrong_class)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a     = Named_object::make(registry, "a");
    std::shared_ptr<Test_object>  plain = std::make_shared<Test_object>();

    a->set_value(weak_target_property, Weak_object_reference{plain});
    EXPECT_FALSE(a->has_local_value(weak_target_property.get()));
    EXPECT_FALSE(get_referenced_object(a->get_value(weak_target_property)));
}

TEST(Weak_object_reference_property, copy_carries_the_value_and_property_set_compares_identity)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a = Named_object::make(registry, "a");
    std::shared_ptr<Named_object> b = Named_object::make(registry, "b");
    std::shared_ptr<Named_object> c = Named_object::make(registry, "c");

    a->set_value(weak_target_property, Weak_object_reference{b});
    Named_object copy{*a};
    EXPECT_EQ(get_referenced_object(copy.get_value(weak_target_property)).get(), b.get());

    const Property_set bag_a = Property_set::read_local_values(*a);
    const Property_set bag_c = Property_set::read_local_values(copy);
    EXPECT_TRUE(bag_a == bag_c);

    copy.set_value(weak_target_property, Weak_object_reference{c});
    const Property_set bag_d = Property_set::read_local_values(copy);
    EXPECT_FALSE(bag_a == bag_d);
    EXPECT_EQ(Property_set::diff(bag_a, bag_d).size(), std::size_t{1});
}

TEST(Weak_object_reference_property, expressions_exclude_weak_object_properties)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a = Named_object::make(registry, "a");
    std::shared_ptr<Named_object> b = Named_object::make(registry, "b");
    a->set_value(weak_target_property, Weak_object_reference{b});

    EXPECT_FALSE(a->set_expression(weak_target_property.get(), "1"));
    EXPECT_EQ(get_referenced_object(a->get_value(weak_target_property)).get(), b.get());

    EXPECT_TRUE(a->set_expression(scalar_property.get(), "{weak_target}"));
    static_cast<void>(a->get_value(scalar_property));
    EXPECT_NE(a->get_expression_error(scalar_property.get()).find("object reference"), std::string_view::npos);
    a->clear_value(scalar_property);
}

TEST(Member_backed_property, weak_ptr_member_round_trips_through_traits)
{
    std::vector<std::shared_ptr<Named_object>> registry;
    std::shared_ptr<Named_object> a = Named_object::make(registry, "a");
    std::shared_ptr<Named_object> b = Named_object::make(registry, "b");

    // The member is the value.
    a->observer = b;
    EXPECT_EQ(get_referenced_object(a->get_value(observer_property)).get(), b.get());
    EXPECT_EQ(a->get_value_source(observer_property.get()), Value_source::local);

    // set writes the member and runs after_set.
    a->set_value(observer_property, Weak_object_reference{});
    EXPECT_FALSE(a->observer.lock());
    EXPECT_EQ(a->after_set_calls, 1);

    a->set_value(observer_property, Weak_object_reference{b});
    EXPECT_EQ(a->observer.lock().get(), b.get());
    EXPECT_EQ(a->after_set_calls, 2);

    // Unchanged: neither the write nor after_set.
    a->set_value(observer_property, Weak_object_reference{b});
    EXPECT_EQ(a->after_set_calls, 2);

    // The traits' validate rejects a live pointee that is not a Named_object.
    std::shared_ptr<Test_object> plain = std::make_shared<Test_object>();
    a->set_value(observer_property, Weak_object_reference{plain});
    EXPECT_EQ(a->observer.lock().get(), b.get());
    EXPECT_EQ(a->after_set_calls, 2);

    // An expired target reads as null.
    forget(registry, b);
    b.reset();
    EXPECT_FALSE(get_referenced_object(a->get_value(observer_property)));

    a->clear_value(observer_property);
    EXPECT_FALSE(a->observer.lock());
}
