#include "test_object.hpp"
#include "erhe_property/property_set.hpp"

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

// An object whose "position" lives in a plain member, exposed as a bridged
// property (D18).
class Bridged_object : public Test_object
{
public:
    Bridged_object() : Test_object{type_d()} {}
    glm::vec3 position{0.0f};
    int       set_calls{0};
};

const Property<glm::vec3> bridged_position = Property<glm::vec3>::register_property(
    "bridged_position", type_d(),
    Property_metadata{
        .default_value = glm::vec3{1.0f, 2.0f, 3.0f},
        .bridge = Property_bridge{
            .get = [](const Dependency_object& o) -> Property_value { return static_cast<const Bridged_object&>(o).position; },
            .set = [](Dependency_object& o, const Property_value& v) {
                Bridged_object& b = static_cast<Bridged_object&>(o);
                b.position = std::get<glm::vec3>(v);
                ++b.set_calls;
            }
        }
    }
);

const Property<int> bridged_plain = Property<int>::register_property("bridged_plain", type_d());

} // anonymous namespace

TEST(Bridged_property, reads_and_writes_go_through_the_bridge)
{
    Bridged_object o;
    EXPECT_EQ(o.get_value(bridged_position), glm::vec3{0.0f}); // the member, not the metadata default
    EXPECT_TRUE(o.has_local_value(bridged_position.get()));
    EXPECT_EQ(o.get_value_source(bridged_position.get()), Value_source::local);
    EXPECT_EQ(o.read_local_value(bridged_position).value(), glm::vec3{0.0f});

    o.set_value(bridged_position, glm::vec3{4.0f, 5.0f, 6.0f});
    EXPECT_EQ(o.position, (glm::vec3{4.0f, 5.0f, 6.0f}));
    EXPECT_EQ(o.set_calls, 1);
    EXPECT_EQ(o.change_count("bridged_position"), std::size_t{1});
    EXPECT_EQ(std::get<glm::vec3>(o.changes.back().old_value), glm::vec3{0.0f});

    // Same value: bridge set runs (the object owns the storage), no notification.
    o.set_value(bridged_position, glm::vec3{4.0f, 5.0f, 6.0f});
    EXPECT_EQ(o.change_count("bridged_position"), std::size_t{1});

    // Clear writes the default layer: the registry default (no per-object one).
    o.clear_value(bridged_position);
    EXPECT_EQ(o.position, (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(o.change_count("bridged_position"), std::size_t{2});
    EXPECT_FALSE(o.is_coerced(bridged_position.get()));
}

TEST(Bridged_property, listed_among_local_values_in_index_order)
{
    Bridged_object o;
    o.set_value(bridged_plain, 7);
    std::vector<std::string> names;
    o.for_each_local_value([&](const Dependency_property& p, const Property_value&) { names.emplace_back(p.get_name()); });
    ASSERT_EQ(names.size(), std::size_t{2});
    EXPECT_EQ(names[0], "bridged_position");
    EXPECT_EQ(names[1], "bridged_plain");

    const Property_set bag = Property_set::read_local_values(o);
    EXPECT_EQ(bag.find(bridged_position.get()).value(), Property_value{glm::vec3{0.0f}});

    Bridged_object copy_target;
    bag.apply(copy_target);
    EXPECT_EQ(copy_target.position, glm::vec3{0.0f});
    EXPECT_EQ(copy_target.get_value(bridged_plain), 7);
}

TEST(Bridged_property, observers_and_batches_apply)
{
    Bridged_object o;
    int calls = 0;
    Observer_token token = o.add_observer(bridged_position.get(), [&](Dependency_object&, const Property_changed_args&) { ++calls; });
    {
        const Dependency_object::Change_batch batch{o};
        o.set_value(bridged_position, glm::vec3{1.0f});
        o.set_value(bridged_position, glm::vec3{2.0f});
        EXPECT_EQ(calls, 0);
    }
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(o.position, glm::vec3{2.0f});
}

namespace {

// A bridged property with a per-object default (D31): the default layer is
// the object's own value, which a clear writes through the bridge.
auto type_bridge_default() -> Owner_type
{
    static const Owner_type id = allocate_owner_type(root_owner_type, "type_bridge_default");
    return id;
}

class Bridged_default_object : public Test_object
{
public:
    explicit Bridged_default_object(const float default_value)
        : Test_object      {type_bridge_default()}
        , per_object_default{default_value}
    {
    }
    float value             {0.0f};
    float per_object_default{0.0f};
};

const Property<float> bridged_with_object_default = Property<float>::register_property(
    "bridged_with_object_default", type_bridge_default(),
    Property_metadata{
        .default_value = 1.0f,
        .bridge = Property_bridge{
            .get = [](const Dependency_object& o) -> Property_value { return static_cast<const Bridged_default_object&>(o).value; },
            .set = [](Dependency_object& o, const Property_value& v) {
                static_cast<Bridged_default_object&>(o).value = std::get<float>(v);
            }
        },
        .compute_default = [](const Dependency_object& o) -> Property_value {
            return static_cast<const Bridged_default_object&>(o).per_object_default;
        }
    }
);

} // anonymous namespace

TEST(Bridged_property, clear_writes_the_per_object_default)
{
    Bridged_default_object o{5.0f};
    o.set_value(bridged_with_object_default, 7.0f);
    EXPECT_EQ(o.value, 7.0f);
    EXPECT_EQ(std::get<float>(o.get_default_value(bridged_with_object_default.get())), 5.0f);

    EXPECT_TRUE(o.clear_value(bridged_with_object_default.get()));
    EXPECT_EQ(o.value, 5.0f) << "the object's default layer, not the registry default 1.0";
    EXPECT_EQ(o.get_value(bridged_with_object_default), 5.0f);
}
