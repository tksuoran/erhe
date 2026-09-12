// Animated layer (D5 / R3 in doc/property-system.md): coerced > animated >
// local > style > reference > inherited > default. An animated value is a
// playback pose: it is read above the local layer, it is never authored, and
// a write made while it runs goes to the base under it.

#include "test_object.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

const Property<float> an_plain = Property<float>::register_property("an_plain", type_c(), Property_metadata{.default_value = 1.0f});
const Property<float> an_inh   = Property<float>::register_property("an_inh",   type_c(), Property_metadata{.default_value = 0.0f, .inherits = true});
const Property<float> an_clamp = Property<float>::register_property(
    "an_clamp", type_c(),
    Property_metadata{
        .default_value = 0.0f,
        .coerce        = [](const Dependency_object&, const Property_value& value) -> Property_value {
            return std::min(std::get<float>(value), 10.0f);
        }
    }
);
const Property<float> an_positive = Property<float>::register_property(
    "an_positive", type_c(),
    Property_metadata{.default_value = 1.0f},
    [](const Property_value& value) -> bool { return std::get<float>(value) > 0.0f; }
);

const Property<float> an_computed = Property<float>::register_computed(
    "an_computed", type_c(),
    [](const Dependency_object&) -> Property_value { return 3.0f; }
);

// An object whose "offset" lives in a plain member behind a bridge (D18).
class Animated_bridged_object : public Test_object
{
public:
    Animated_bridged_object() : Test_object{type_e()} {}
    float offset{0.0f};
};

const Property<float> an_bridged = Property<float>::register_property(
    "an_bridged", type_e(),
    Property_metadata{
        .default_value = 7.0f,
        .bridge = Property_bridge{
            .get = [](const Dependency_object& o) -> Property_value { return static_cast<const Animated_bridged_object&>(o).offset; },
            .set = [](Dependency_object& o, const Property_value& v) { static_cast<Animated_bridged_object&>(o).offset = std::get<float>(v); }
        }
    }
);

[[nodiscard]] auto local_value_names(const Dependency_object& object) -> std::vector<std::string>
{
    std::vector<std::string> names;
    object.for_each_local_value([&names](const Dependency_property& property, const Property_value&) { names.emplace_back(property.get_name()); });
    return names;
}

} // anonymous namespace

TEST(Animated_layer, entry_store_reads_above_the_local_value)
{
    Test_object object{type_c()};
    object.set_value(an_plain, 2.0f);

    EXPECT_TRUE(object.set_animated_value(an_plain, 5.0f));
    EXPECT_EQ(object.get_value(an_plain), 5.0f);
    EXPECT_EQ(object.get_value_source(an_plain.get()), Value_source::animated);
    EXPECT_TRUE(object.has_animated_value(an_plain.get()));

    // The base is the local value, and the local layer is untouched.
    EXPECT_EQ(object.get_animation_base_value(an_plain), 2.0f);
    EXPECT_TRUE(object.has_local_value(an_plain.get()));
    EXPECT_TRUE(object.has_own_value(an_plain.get()));
    EXPECT_EQ(object.read_local_value(an_plain).value(), 2.0f);
    EXPECT_EQ(local_value_names(object), (std::vector<std::string>{"an_plain"}));

    // A write while the animation runs edits the base only.
    EXPECT_TRUE(object.set_value(an_plain.get(), Property_value{3.0f}));
    EXPECT_EQ(object.get_value(an_plain), 5.0f);
    EXPECT_EQ(object.get_animation_base_value(an_plain), 3.0f);
    EXPECT_EQ(object.read_local_value(an_plain).value(), 3.0f);

    EXPECT_TRUE(object.clear_animated_value(an_plain.get()));
    EXPECT_FALSE(object.has_animated_value(an_plain.get()));
    EXPECT_EQ(object.get_value(an_plain), 3.0f);
    EXPECT_EQ(object.get_value_source(an_plain.get()), Value_source::local);
}

TEST(Animated_layer, an_animated_value_alone_is_not_an_authored_value)
{
    Test_object object{type_c()};
    EXPECT_TRUE(object.set_animated_value(an_plain, 4.0f));

    EXPECT_EQ(object.get_value(an_plain), 4.0f);
    EXPECT_EQ(object.get_value_source(an_plain.get()), Value_source::animated);
    EXPECT_FALSE(object.has_local_value(an_plain.get()));
    EXPECT_FALSE(object.has_own_value(an_plain.get()));
    EXPECT_FALSE(object.read_local_value(an_plain).has_value());
    EXPECT_FALSE(object.read_local_state(an_plain.get()).has_value());
    EXPECT_TRUE(local_value_names(object).empty());

    // Without a local value under it the base is the default.
    EXPECT_EQ(object.get_animation_base_value(an_plain), 1.0f);

    EXPECT_TRUE(object.clear_animated_value(an_plain.get()));
    EXPECT_EQ(object.get_value(an_plain), 1.0f);
    EXPECT_EQ(object.get_value_source(an_plain.get()), Value_source::default_value);
}

TEST(Animated_layer, set_and_clear_notify_like_a_local_write)
{
    Test_object object{type_c()};
    object.set_value(an_plain, 2.0f);
    object.changes.clear();

    std::vector<Recorded_change> observed;
    const Observer_token token = object.add_observer(
        an_plain.get(),
        [&observed](Dependency_object&, const Property_changed_args& args) {
            observed.push_back(
                Recorded_change{
                    .property_name = std::string{args.property.get_name()},
                    .old_value     = args.old_value,
                    .new_value     = args.new_value,
                    .old_source    = args.old_source,
                    .new_source    = args.new_source
                }
            );
        }
    );

    object.set_animated_value(an_plain, 5.0f);
    ASSERT_EQ(observed.size(), std::size_t{1});
    EXPECT_EQ(std::get<float>(observed.back().old_value), 2.0f);
    EXPECT_EQ(std::get<float>(observed.back().new_value), 5.0f);
    EXPECT_EQ(observed.back().old_source, Value_source::local);
    EXPECT_EQ(observed.back().new_source, Value_source::animated);
    EXPECT_EQ(object.change_count("an_plain"), std::size_t{1});

    object.clear_animated_value(an_plain.get());
    ASSERT_EQ(observed.size(), std::size_t{2});
    EXPECT_EQ(observed.back().old_source, Value_source::animated);
    EXPECT_EQ(observed.back().new_source, Value_source::local);
    EXPECT_EQ(std::get<float>(observed.back().new_value), 2.0f);

    // Clearing a property that is not animated changes and notifies nothing.
    object.clear_animated_value(an_plain.get());
    EXPECT_EQ(observed.size(), std::size_t{2});
}

TEST(Animated_layer, coerce_applies_to_the_animated_value)
{
    Test_object object{type_c()};
    object.set_value(an_clamp, 4.0f);
    EXPECT_TRUE(object.set_animated_value(an_clamp, 25.0f));
    EXPECT_EQ(object.get_value(an_clamp), 10.0f);              // coerced
    EXPECT_EQ(object.get_value_source(an_clamp.get()), Value_source::animated);
    EXPECT_EQ(object.get_animation_base_value(an_clamp), 4.0f);
    EXPECT_FALSE(object.is_coerced(an_clamp.get()));           // the LOCAL value is not coerced

    object.clear_animated_value(an_clamp.get());
    EXPECT_EQ(object.get_value(an_clamp), 4.0f);
}

TEST(Animated_layer, validate_rejects_a_bad_animated_value)
{
    Test_object object{type_c()};
    object.set_value(an_positive, 2.0f);
    EXPECT_FALSE(object.set_animated_value(an_positive, -1.0f));
    EXPECT_FALSE(object.has_animated_value(an_positive.get()));
    EXPECT_EQ(object.get_value(an_positive), 2.0f);
}

TEST(Animated_layer, descendants_inherit_the_animated_value)
{
    Test_object parent{type_c()};
    Test_object child{type_c()};
    child.set_parent(&parent);
    parent.set_value(an_inh, 2.0f);
    EXPECT_EQ(child.get_value(an_inh), 2.0f);
    child.changes.clear();

    parent.set_animated_value(an_inh, 6.0f);
    EXPECT_EQ(child.get_value(an_inh), 6.0f);
    EXPECT_EQ(child.get_value_source(an_inh.get()), Value_source::inherited);
    EXPECT_EQ(child.change_count("an_inh"), std::size_t{1});

    parent.clear_animated_value(an_inh.get());
    EXPECT_EQ(child.get_value(an_inh), 2.0f);
    EXPECT_EQ(child.change_count("an_inh"), std::size_t{2});
}

TEST(Animated_layer, a_sealed_object_accepts_an_animated_value)
{
    Test_object object{type_c()};
    object.set_value(an_plain, 2.0f);
    object.seal();

    EXPECT_FALSE(object.set_value(an_plain.get(), Property_value{3.0f})); // the authored layer is sealed
    EXPECT_TRUE(object.set_animated_value(an_plain, 5.0f));
    EXPECT_EQ(object.get_value(an_plain), 5.0f);
    EXPECT_TRUE(object.clear_animated_value(an_plain.get()));
    EXPECT_EQ(object.get_value(an_plain), 2.0f);
}

TEST(Animated_layer, a_computed_property_refuses_an_animated_value)
{
    Test_object object{type_c()};
    EXPECT_FALSE(object.set_animated_value(an_computed, 5.0f));
    EXPECT_EQ(object.get_value(an_computed), 3.0f);
}

TEST(Animated_layer, bridged_storage_carries_the_animated_value_and_the_entry_the_base)
{
    Animated_bridged_object object;
    object.set_value(an_bridged, 2.0f);
    ASSERT_EQ(object.offset, 2.0f);

    EXPECT_TRUE(object.set_animated_value(an_bridged, 5.0f));
    EXPECT_EQ(object.offset, 5.0f);                            // the bridge holds the pose
    EXPECT_EQ(object.get_value(an_bridged), 5.0f);
    EXPECT_EQ(object.get_value_source(an_bridged.get()), Value_source::animated);
    EXPECT_EQ(object.get_animation_base_value(an_bridged), 2.0f);
    EXPECT_EQ(object.read_local_value(an_bridged).value(), 2.0f);

    std::vector<Property_value> local_values;
    object.for_each_local_value(
        [&local_values](const Dependency_property& property, const Property_value& value) {
            if (property.get_name() == "an_bridged") {
                local_values.push_back(value);
            }
        }
    );
    ASSERT_EQ(local_values.size(), std::size_t{1});
    EXPECT_EQ(std::get<float>(local_values.front()), 2.0f);    // the kept base, not the pose

    // A write while the animation runs edits the base, not the storage.
    EXPECT_TRUE(object.set_value(an_bridged.get(), Property_value{3.0f}));
    EXPECT_EQ(object.offset, 5.0f);
    EXPECT_EQ(object.get_animation_base_value(an_bridged), 3.0f);

    // A second animated value keeps the base already kept.
    EXPECT_TRUE(object.set_animated_value(an_bridged, 8.0f));
    EXPECT_EQ(object.offset, 8.0f);
    EXPECT_EQ(object.get_animation_base_value(an_bridged), 3.0f);

    EXPECT_TRUE(object.clear_animated_value(an_bridged.get()));
    EXPECT_EQ(object.offset, 3.0f);                            // the base is written back
    EXPECT_FALSE(object.has_animated_value(an_bridged.get()));
    EXPECT_EQ(object.get_value(an_bridged), 3.0f);
    EXPECT_EQ(object.get_value_source(an_bridged.get()), Value_source::local);
    EXPECT_EQ(object.read_local_value(an_bridged).value(), 3.0f);
}

TEST(Animated_layer, a_sealed_bridged_object_accepts_an_animated_value)
{
    Animated_bridged_object object;
    object.set_value(an_bridged, 2.0f);
    object.seal();
    EXPECT_TRUE(object.set_animated_value(an_bridged, 5.0f));
    EXPECT_EQ(object.offset, 5.0f);
    EXPECT_TRUE(object.clear_animated_value(an_bridged.get()));
    EXPECT_EQ(object.offset, 2.0f);
}

TEST(Animated_layer, value_source_has_a_name)
{
    EXPECT_STREQ(c_str(Value_source::animated), "animated");
}
