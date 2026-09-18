// Computed properties (doc/property_system.md D26): a read-only
// property whose effective value the owner provides on every read; no
// layer applies, nothing is stored, and the owner pushes changes to
// expressions with invalidate_dependents.

#include "test_object.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_style.hpp"

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

// An object whose "extent" is derived from a plain member.
class Computed_object : public Test_object
{
public:
    Computed_object() : Test_object{type_d()} {}
    glm::vec3 size{1.0f, 2.0f, 3.0f};
    int       compute_calls{0};
};

const Property<glm::vec3> computed_extent = Property<glm::vec3>::register_computed(
    "computed_extent", type_d(),
    [](const Dependency_object& o) -> Property_value {
        Computed_object& c = const_cast<Computed_object&>(static_cast<const Computed_object&>(o));
        ++c.compute_calls;
        return c.size * 0.5f;
    },
    Property_metadata{
        .coerce = [](const Dependency_object&, const Property_value&) -> Property_value { return glm::vec3{-1.0f}; }, // never runs on a computed property
        .ui     = Property_ui{.group = "Derived"}
    }
);

const Property<int> computed_count = Property<int>::register_computed(
    "computed_count", type_d(),
    [](const Dependency_object& o) -> Property_value { return static_cast<int>(static_cast<const Computed_object&>(o).size.x); }
);

const Property<float> computed_target = Property<float>::register_property("computed_target", type_d());

// A writable computed property: twice computed_target, and its setter
// writes computed_target.
const Property<float> computed_double = Property<float>::register_computed(
    "computed_double", type_d(),
    [](const Dependency_object& o) -> Property_value { return o.get_value(computed_target) * 2.0f; },
    [](Dependency_object& o, const Property_value& value) { o.set_value(computed_target, std::get<float>(value) * 0.5f); },
    computed_target.get()
);

} // anonymous namespace

TEST(Computed_property, reads_the_provider_with_source_computed_and_no_layers)
{
    Computed_object o;
    EXPECT_TRUE(computed_extent.get().is_read_only());
    EXPECT_TRUE(computed_extent.get().get_default_metadata().is_computed());
    EXPECT_EQ(o.get_value(computed_extent), (glm::vec3{0.5f, 1.0f, 1.5f}));
    EXPECT_EQ(o.get_value_source(computed_extent.get()), Value_source::computed);
    EXPECT_FALSE(o.has_local_value(computed_extent.get()));
    EXPECT_FALSE(o.read_local_value(computed_extent).has_value());
    EXPECT_FALSE(o.is_coerced(computed_extent.get()));
    EXPECT_EQ(o.get_value(computed_count), 1);

    // Every read goes to the provider: a member change is visible at once.
    o.size = glm::vec3{4.0f, 6.0f, 8.0f};
    EXPECT_EQ(o.get_value(computed_extent), (glm::vec3{2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(o.get_value(computed_count), 4);
    EXPECT_GE(o.compute_calls, 2);
}

TEST(Computed_property, every_write_is_rejected_without_notification)
{
    Computed_object o;
    EXPECT_FALSE(o.set_value(computed_extent.get(), Property_value{glm::vec3{9.0f}}));
    EXPECT_FALSE(o.set_current_value(computed_extent.get(), Property_value{glm::vec3{9.0f}}));
    EXPECT_FALSE(o.clear_value(computed_extent.get()));
    EXPECT_FALSE(o.set_expression(computed_extent.get(), "1, 2, 3"));
    EXPECT_FALSE(o.apply_local_state(computed_extent.get(), Local_state{Property_value{glm::vec3{9.0f}}}));
    EXPECT_FALSE(o.get_expression(computed_extent.get()).has_value());
    EXPECT_EQ(o.get_value(computed_extent), (glm::vec3{0.5f, 1.0f, 1.5f}));
    EXPECT_EQ(o.size, (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_TRUE(o.changes.empty());
    EXPECT_FALSE(o.read_local_state(computed_extent.get()).has_value());
}

TEST(Computed_property, writable_one_sets_through_its_setter_and_keeps_no_layer)
{
    Computed_object o;
    const Dependency_property& property = computed_double.get();
    EXPECT_FALSE(property.is_read_only());
    EXPECT_TRUE(property.get_default_metadata().is_computed());
    EXPECT_TRUE(property.get_default_metadata().is_computed_writable());
    EXPECT_EQ(property.get_default_metadata().compute_writes, computed_target.get_ptr());
    EXPECT_EQ(o.get_value(computed_double), 0.0f);
    EXPECT_EQ(o.get_value_source(property), Value_source::computed);

    // A set goes to the stored property: that one notifies, this one has
    // no entry and no notification of its own.
    EXPECT_TRUE(o.set_value(property, Property_value{8.0f}));
    EXPECT_EQ(o.get_value(computed_target), 4.0f);
    EXPECT_EQ(o.get_value(computed_double), 8.0f);
    EXPECT_TRUE(o.has_local_value(computed_target.get()));
    EXPECT_FALSE(o.has_local_value(property));
    EXPECT_FALSE(o.read_local_state(property).has_value());
    EXPECT_EQ(o.change_count("computed_target"), std::size_t{1});
    EXPECT_EQ(o.change_count("computed_double"), std::size_t{0});
    EXPECT_TRUE(o.set_current_value(property, Property_value{2.0f}));
    EXPECT_EQ(o.get_value(computed_target), 1.0f);

    // No local layer: clearing, an expression and a null local state are
    // rejected and change nothing.
    EXPECT_FALSE(o.clear_value(property));
    EXPECT_FALSE(o.set_expression(property, "3"));
    EXPECT_FALSE(o.apply_local_state(property, std::nullopt));
    EXPECT_FALSE(o.apply_local_state(property, Local_state{Expression_text{"3"}}));
    EXPECT_EQ(o.get_value(computed_target), 1.0f);
    // A value through apply_local_state goes to the setter too.
    EXPECT_TRUE(o.apply_local_state(property, Local_state{Property_value{6.0f}}));
    EXPECT_EQ(o.get_value(computed_target), 3.0f);

    // Not a local value for bags: the stored property is.
    const Property_set bag = Property_set::read_local_values(o);
    EXPECT_FALSE(bag.contains(property));
    EXPECT_TRUE(bag.contains(computed_target.get()));

    // A sealed object refuses the setter too.
    o.seal();
    EXPECT_FALSE(o.set_value(property, Property_value{20.0f}));
    EXPECT_EQ(o.get_value(computed_target), 3.0f);
}

TEST(Computed_property, is_not_a_local_value_for_bags_copies_or_styles)
{
    Computed_object o;
    o.set_value(computed_target, 2.0f);

    bool target_listed   = false;
    bool computed_listed = false;
    o.for_each_local_value(
        [&](const Dependency_property& property, const Property_value&) {
            target_listed   = target_listed   || (&property == computed_target.get_ptr());
            computed_listed = computed_listed || (&property == computed_extent.get_ptr()) || (&property == computed_count.get_ptr());
        }
    );
    EXPECT_TRUE(target_listed);
    EXPECT_FALSE(computed_listed);

    const Property_set bag = Property_set::read_local_values(o);
    EXPECT_FALSE(bag.contains(computed_extent.get()));
    EXPECT_TRUE(bag.contains(computed_target.get()));

    // A copy carries no computed state; it reads its own member.
    Computed_object copy{o};
    copy.size = glm::vec3{10.0f};
    EXPECT_EQ(copy.get_value(computed_extent), (glm::vec3{5.0f}));
    EXPECT_EQ(o.get_value(computed_extent), (glm::vec3{0.5f, 1.0f, 1.5f}));

    // A style entry naming a computed property is ignored (D25 sits below
    // the provider).
    Property_set style_values;
    style_values.set(computed_extent.get(), Property_value{glm::vec3{7.0f}});
    o.set_style(std::make_shared<const Property_style>("s", std::move(style_values)));
    EXPECT_EQ(o.get_value(computed_extent), (glm::vec3{0.5f, 1.0f, 1.5f}));
    EXPECT_EQ(o.get_value_source(computed_extent.get()), Value_source::computed);
    EXPECT_EQ(o.change_count("computed_extent"), std::size_t{0});
}

TEST(Computed_property, is_an_expression_source_pushed_by_invalidate_dependents)
{
    Computed_object o;
    ASSERT_TRUE(o.set_expression(computed_target.get(), "{computed_extent.y} * 10"));
    EXPECT_EQ(o.get_value(computed_target), 10.0f);
    EXPECT_EQ(o.get_value_source(computed_target.get()), Value_source::expression);

    // The member changes behind the provider: nothing re-evaluates until the
    // owner announces it.
    o.size = glm::vec3{1.0f, 4.0f, 3.0f};
    EXPECT_EQ(o.get_value(computed_target), 10.0f);
    const std::size_t before = o.change_count("computed_target");
    o.invalidate_dependents(computed_extent.get());
    EXPECT_EQ(o.get_value(computed_target), 20.0f);
    EXPECT_EQ(o.change_count("computed_target"), before + 1);
    EXPECT_EQ(std::get<float>(o.changes.back().old_value), 10.0f);
    EXPECT_EQ(std::get<float>(o.changes.back().new_value), 20.0f);

    // Nothing depends on computed_count: the call is a no-op.
    o.invalidate_dependents(computed_count.get());
    EXPECT_EQ(o.change_count("computed_target"), before + 1);
}
