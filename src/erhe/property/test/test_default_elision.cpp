// Default elision (doc/property-system.md D32): a local value is an
// authored value, so an importer's field-by-field fill is followed by
// clear_default_valued_local_properties, which takes back every local
// value that merely repeats the object's own default layer (D31).

#include "test_object.hpp"

#include "erhe_property/property_set.hpp"
#include "erhe_property/property_style.hpp"

#include <memory>
#include <utility>

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

auto type_elision() -> Owner_type
{
    static const Owner_type id = allocate_owner_type(root_owner_type, "type_elision");
    return id;
}

// An object whose "seeded" default follows a plain member (D31).
class Elision_object : public Test_object
{
public:
    Elision_object() : Test_object{type_elision()} {}

    int seed{0};
};

const Property<float> elision_plain = Property<float>::register_property(
    "elision_plain", type_elision(), Property_metadata{.default_value = 2.5f}
);
const Property<bool> elision_inherited = Property<bool>::register_property(
    "elision_inherited", type_elision(), Property_metadata{.default_value = false, .inherits = true}
);
const Property<int> elision_session = Property<int>::register_property(
    "elision_session", type_elision(), Property_metadata{.default_value = 4, .flags = Property_flags::none}
);
const Property<int> elision_seeded = Property<int>::register_property(
    "elision_seeded", type_elision(),
    Property_metadata{
        .default_value   = 7,
        .compute_default = [](const Dependency_object& object) -> Property_value {
            return static_cast<const Elision_object&>(object).seed * 10;
        }
    }
);

} // namespace

TEST(Default_elision, a_local_value_equal_to_the_default_is_cleared)
{
    Elision_object object{};
    object.set_value(elision_plain, 2.5f);
    EXPECT_EQ(object.get_value_source(elision_plain.get()), Value_source::local);

    clear_default_valued_local_properties(object);

    EXPECT_EQ(object.get_value_source(elision_plain.get()), Value_source::default_value);
    EXPECT_EQ(object.get_value(elision_plain), 2.5f);
}

TEST(Default_elision, an_authored_value_stays_local)
{
    Elision_object object{};
    object.set_value(elision_plain, 3.5f);

    clear_default_valued_local_properties(object);

    EXPECT_EQ(object.get_value_source(elision_plain.get()), Value_source::local);
    EXPECT_EQ(object.get_value(elision_plain), 3.5f);
}

// D31: the comparison is against the object's own default, not the
// registration-time one.
TEST(Default_elision, the_comparison_uses_the_per_object_default)
{
    Elision_object a{};
    Elision_object b{};
    b.seed = 3;
    a.set_value(elision_seeded, 0);  // a's default
    b.set_value(elision_seeded, 30); // b's default
    Elision_object c{};
    c.seed = 3;
    c.set_value(elision_seeded, 7);  // the metadata default, not c's

    clear_default_valued_local_properties(a);
    clear_default_valued_local_properties(b);
    clear_default_valued_local_properties(c);

    EXPECT_EQ(a.get_value_source(elision_seeded.get()), Value_source::default_value);
    EXPECT_EQ(b.get_value_source(elision_seeded.get()), Value_source::default_value);
    EXPECT_EQ(c.get_value_source(elision_seeded.get()), Value_source::local);
    EXPECT_EQ(c.get_value(elision_seeded), 7);
}

// Clearing must never move an effective value: an inherited opinion below
// the local one means the local value is this object's own opinion.
TEST(Default_elision, a_value_shadowing_an_inherited_one_stays_local)
{
    Elision_object parent{};
    Elision_object child{};
    child.set_parent(&parent);
    parent.set_value(elision_inherited, true);
    child.set_value(elision_inherited, false); // the default, but it shadows

    clear_default_valued_local_properties(child);

    EXPECT_EQ(child.get_value_source(elision_inherited.get()), Value_source::local);
    EXPECT_EQ(child.get_value(elision_inherited), false);
}

TEST(Default_elision, a_value_shadowing_a_style_stays_local)
{
    Property_set style_values;
    style_values.set(elision_plain.get(), Property_value{9.0f});
    const std::shared_ptr<const Property_style> style = std::make_shared<const Property_style>("style", std::move(style_values));

    Elision_object object{};
    EXPECT_TRUE(object.set_style(style));
    object.set_value(elision_plain, 2.5f);

    clear_default_valued_local_properties(object);

    EXPECT_EQ(object.get_value_source(elision_plain.get()), Value_source::local);
    EXPECT_EQ(object.get_value(elision_plain), 2.5f);
}

// Session state (no Property_flags::serialize) is not part of the authored
// set, and an expression is the authored layer itself.
TEST(Default_elision, non_serialized_and_driven_values_are_left_alone)
{
    Elision_object object{};
    object.set_value(elision_session, 4);
    EXPECT_TRUE(object.set_expression(elision_plain.get(), "2.5"));

    clear_default_valued_local_properties(object);

    EXPECT_EQ(object.get_value_source(elision_session.get()), Value_source::local);
    EXPECT_EQ(object.get_value_source(elision_plain.get()), Value_source::expression);
}

TEST(Default_elision, a_sealed_object_is_left_alone)
{
    Elision_object object{};
    object.set_value(elision_plain, 2.5f);
    object.seal();

    clear_default_valued_local_properties(object);

    EXPECT_EQ(object.get_value_source(elision_plain.get()), Value_source::local);
}
