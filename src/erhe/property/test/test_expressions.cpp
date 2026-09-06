#include "test_object.hpp"
#include "erhe_property/enum_info.hpp"
#include "erhe_property/expression.hpp"
#include "erhe_property/property_set.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

auto type_e() -> Owner_type { static const Owner_type id = allocate_owner_type(root_owner_type, "type_e"); return id; } // expression tests
auto type_f() -> Owner_type { static const Owner_type id = allocate_owner_type(root_owner_type, "type_f"); return id; } // expression tests, bridged target

// Test_object that resolves reference paths the way Item_base does: "" is
// the object, ".." its parent, anything else a name among the live objects.
class Named_object : public Test_object
{
public:
    explicit Named_object(std::string name, const Owner_type type = type_e())
        : Test_object{type}
        , m_name     {std::move(name)}
    {
        registry().push_back(this);
    }
    Named_object(const Named_object& other)
        : Test_object{other}
        , m_name     {other.m_name + "_copy"}
    {
        registry().push_back(this);
    }
    ~Named_object() noexcept override
    {
        std::erase(registry(), this);
    }

    auto resolve_expression_object(const std::string_view path) const -> Dependency_object* override
    {
        if (path.empty()) {
            return const_cast<Named_object*>(this);
        }
        if (path == "..") {
            return const_cast<Dependency_object*>(get_inheritance_parent());
        }
        for (Named_object* object : registry()) {
            if (object->m_name == path) {
                return object;
            }
        }
        return nullptr;
    }

    void rename(std::string name) { m_name = std::move(name); }

    static auto registry() -> std::vector<Named_object*>&
    {
        static std::vector<Named_object*> objects;
        return objects;
    }

private:
    std::string m_name;
};

enum class Mode : int { off = 0, low = 1, high = 4 };
constexpr Enum_entry c_mode_entries[] = {
    {"Off",  static_cast<int32_t>(Mode::off)},
    {"Low",  static_cast<int32_t>(Mode::low)},
    {"High", static_cast<int32_t>(Mode::high)},
};
const Enum_info c_mode_info{"Mode", c_mode_entries};

const Property<float>       ex_float  = Property<float>::register_property("ex_float", type_e());
const Property<float>       ex_source = Property<float>::register_property("ex_source", type_e(), Property_metadata{.default_value = 2.0f});
const Property<int>         ex_int    = Property<int>::register_property("ex_int", type_e());
const Property<bool>        ex_bool   = Property<bool>::register_property("ex_bool", type_e());
const Property<glm::vec2>   ex_vec2   = Property<glm::vec2>::register_property("ex_vec2", type_e());
const Property<glm::vec3>   ex_vec3   = Property<glm::vec3>::register_property("ex_vec3", type_e(), Property_metadata{.default_value = glm::vec3{1.0f, 2.0f, 3.0f}});
const Property<glm::vec3>   ex_other  = Property<glm::vec3>::register_property("ex_other", type_e());
const Property<glm::quat>   ex_quat   = Property<glm::quat>::register_property("ex_quat", type_e());
const Property<std::string> ex_string = Property<std::string>::register_property("ex_string", type_e());
const Property<glm::ivec3>  ex_ivec3  = Property<glm::ivec3>::register_property("ex_ivec3", type_e());
const Property<Mode>        ex_mode   = Property<Mode>::register_property("ex_mode", type_e(), c_mode_info);
const Property<double>      ex_double = Property<double>::register_property("ex_double", type_e());
const Property<glm::mat4>   ex_mat4   = Property<glm::mat4>::register_property("ex_mat4", type_e());
const Property<Asset_path>  ex_asset  = Property<Asset_path>::register_property("ex_asset", type_e());
const Property<std::vector<float>> ex_floats = Property<std::vector<float>>::register_property("ex_floats", type_e());
const Property<std::vector<int>>   ex_ints   = Property<std::vector<int>>::register_property("ex_ints", type_e());
const Property<float>       ex_clamped = Property<float>::register_property(
    "ex_clamped", type_e(),
    Property_metadata{
        .coerce = [](const Dependency_object&, const Property_value& v) -> Property_value {
            return std::clamp(std::get<float>(v), 0.0f, 1.0f);
        }
    }
);
const Property<float>       ex_positive = Property<float>::register_property(
    "ex_positive", type_e(), Property_metadata{},
    [](const Property_value& v) { return std::get<float>(v) >= 0.0f; }
);
const Property<float>       ex_inherited = Property<float>::register_property("ex_inherited", type_e(), Property_metadata{.default_value = 7.0f, .inherits = true});

class Bridged_target : public Named_object
{
public:
    explicit Bridged_target(std::string name) : Named_object{std::move(name), type_f()} {}
    glm::vec3 position{0.0f};
    int       set_calls{0};
};

const Property<glm::vec3> bridged_target_position = Property<glm::vec3>::register_property(
    "bridged_target_position", type_f(),
    Property_metadata{
        .bridge = Property_bridge{
            .get = [](const Dependency_object& o) -> Property_value { return static_cast<const Bridged_target&>(o).position; },
            .set = [](Dependency_object& o, const Property_value& v) {
                Bridged_target& b = static_cast<Bridged_target&>(o);
                b.position = std::get<glm::vec3>(v);
                ++b.set_calls;
            }
        }
    }
);
const Property<float> bridged_target_scalar = Property<float>::register_property("bridged_target_scalar", type_f());

} // anonymous namespace

TEST(Expressions, rejects_bad_text_without_changing_anything)
{
    Named_object o{"a"};
    o.set_value(ex_float, 1.5f);
    EXPECT_FALSE(o.set_expression(ex_float.get(), ""));            // empty
    EXPECT_FALSE(o.set_expression(ex_float.get(), "1 +"));         // syntax
    EXPECT_FALSE(o.set_expression(ex_float.get(), "{"));           // unterminated reference
    EXPECT_FALSE(o.set_expression(ex_float.get(), "{ex_float}"));  // its own target
    EXPECT_FALSE(o.set_expression(ex_float.get(), "{ex_source.q}")); // bad component
    EXPECT_FALSE(o.set_expression(ex_float.get(), "1, 2"));        // scalar target, two components
    EXPECT_FALSE(o.set_expression(ex_vec3.get(), "1, 2"));         // vec3 target, two components
    EXPECT_FALSE(o.set_expression(ex_string.get(), "1"));          // string target
    EXPECT_FALSE(o.get_expression(ex_float.get()).has_value());
    EXPECT_EQ(o.get_value(ex_float), 1.5f);
    EXPECT_EQ(o.get_value_source(ex_float.get()), Value_source::local);
    EXPECT_FALSE(o.get_expression(ex_string.get()).has_value());
}

TEST(Expressions, constant_and_self_reference_formulas)
{
    Named_object o{"a"};
    o.set_value(ex_source, 3.0f);
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_source} * 2 + 1"));
    EXPECT_EQ(o.get_value(ex_float), 7.0f);
    EXPECT_EQ(o.get_value_source(ex_float.get()), Value_source::expression);
    EXPECT_EQ(o.get_expression(ex_float.get()).value(), "{ex_source} * 2 + 1");
    EXPECT_TRUE(o.get_expression_error(ex_float.get()).empty());
    EXPECT_TRUE(o.has_local_value(ex_float.get()));
    EXPECT_EQ(o.read_local_value(ex_float).value(), 7.0f); // the cached result
    EXPECT_EQ(o.change_count("ex_float"), std::size_t{1});
    EXPECT_EQ(o.changes.back().new_source, Value_source::expression);

    // Push: a change of the source re-evaluates and notifies with old / new.
    o.set_value(ex_source, 5.0f);
    EXPECT_EQ(o.get_value(ex_float), 11.0f);
    EXPECT_EQ(o.change_count("ex_float"), std::size_t{2});
    EXPECT_EQ(std::get<float>(o.changes.back().old_value), 7.0f);
    EXPECT_EQ(std::get<float>(o.changes.back().new_value), 11.0f);

    // Same value: no notification.
    o.set_value(ex_source, 5.0f);
    EXPECT_EQ(o.change_count("ex_float"), std::size_t{2});

    // A constant formula has no references.
    ASSERT_TRUE(o.set_expression(ex_float.get(), "sqrt(16)"));
    EXPECT_EQ(o.get_value(ex_float), 4.0f);
    o.set_value(ex_source, 9.0f);
    EXPECT_EQ(o.get_value(ex_float), 4.0f);
}

TEST(Expressions, references_other_objects_and_the_parent)
{
    Named_object parent{"parent"};
    Named_object child{"child"};
    Named_object driver{"driver"};
    child.set_parent(&parent);
    parent.set_value(ex_vec3, glm::vec3{1.0f, 2.0f, 3.0f});
    driver.set_value(ex_source, 10.0f);

    ASSERT_TRUE(child.set_expression(ex_float.get(), "{../ex_vec3.y} + {driver/ex_source}"));
    EXPECT_EQ(child.get_value(ex_float), 12.0f);

    driver.set_value(ex_source, 20.0f);
    EXPECT_EQ(child.get_value(ex_float), 22.0f);
    parent.set_value(ex_vec3, glm::vec3{1.0f, 5.0f, 3.0f});
    EXPECT_EQ(child.get_value(ex_float), 25.0f);
    EXPECT_EQ(child.change_count("ex_float"), std::size_t{3});

    // Sources are read through the effective value: an inherited source
    // changes when the ancestor does.
    ASSERT_TRUE(child.set_expression(ex_int.get(), "{ex_inherited}"));
    EXPECT_EQ(child.get_value(ex_int), 7);
    parent.set_value(ex_inherited, 9.0f);
    EXPECT_EQ(child.get_value(ex_int), 9);
}

TEST(Expressions, components_and_broadcast)
{
    Named_object o{"a"};
    o.set_value(ex_other, glm::vec3{1.0f, 2.0f, 3.0f});

    ASSERT_TRUE(o.set_expression(ex_vec3.get(), "{ex_other.z}, {ex_other.y}, {ex_other.x}"));
    EXPECT_EQ(o.get_value(ex_vec3), (glm::vec3{3.0f, 2.0f, 1.0f}));

    // One formula broadcasts; a reference without a component follows the
    // component being evaluated.
    ASSERT_TRUE(o.set_expression(ex_vec3.get(), "{ex_other} * 10"));
    EXPECT_EQ(o.get_value(ex_vec3), (glm::vec3{10.0f, 20.0f, 30.0f}));
    o.set_value(ex_other, glm::vec3{4.0f, 5.0f, 6.0f});
    EXPECT_EQ(o.get_value(ex_vec3), (glm::vec3{40.0f, 50.0f, 60.0f}));

    // A scalar source in a vector formula broadcasts its value.
    o.set_value(ex_source, 2.0f);
    ASSERT_TRUE(o.set_expression(ex_vec2.get(), "{ex_source}"));
    EXPECT_EQ(o.get_value(ex_vec2), (glm::vec2{2.0f, 2.0f}));

    // Function arguments are separated by commas inside parentheses.
    ASSERT_TRUE(o.set_expression(ex_vec2.get(), "pow({ex_source}, 3), max({ex_other.x}, {ex_other.y})"));
    EXPECT_EQ(o.get_value(ex_vec2), (glm::vec2{8.0f, 5.0f}));

    // A component the source does not have is an evaluation error; the
    // previous value stays.
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_vec2.z}"));
    EXPECT_FALSE(o.get_expression_error(ex_float.get()).empty());
    EXPECT_EQ(o.get_value(ex_float), 0.0f);

    // Reading a string property is an evaluation error.
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_string}"));
    EXPECT_FALSE(o.get_expression_error(ex_float.get()).empty());
}

TEST(Expressions, target_type_conversions)
{
    Named_object o{"a"};
    o.set_value(ex_source, 3.6f);

    ASSERT_TRUE(o.set_expression(ex_int.get(), "{ex_source}"));
    EXPECT_EQ(o.get_value(ex_int), 4);

    ASSERT_TRUE(o.set_expression(ex_bool.get(), "gt({ex_source}, 3)"));
    EXPECT_TRUE(o.get_value(ex_bool));
    o.set_value(ex_source, 1.0f);
    EXPECT_FALSE(o.get_value(ex_bool));
    EXPECT_EQ(o.get_value(ex_int), 1);

    // bool and enumeration sources read as numbers.
    o.set_value(ex_mode, Mode::high);
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_bool} + {ex_mode}"));
    EXPECT_EQ(o.get_value(ex_float), 4.0f);

    // Enumeration target: rounded, and validated against the table.
    ASSERT_TRUE(o.set_expression(ex_mode.get(), "{ex_source}"));
    EXPECT_EQ(o.get_value(ex_mode), Mode::low);
    o.set_value(ex_source, 2.0f);
    EXPECT_FALSE(o.get_expression_error(ex_mode.get()).empty());
    EXPECT_EQ(o.get_value(ex_mode), Mode::low); // 2 is not a Mode
    o.set_value(ex_source, 4.0f);
    EXPECT_TRUE(o.get_expression_error(ex_mode.get()).empty());
    EXPECT_EQ(o.get_value(ex_mode), Mode::high);

    // Quaternion target takes x y z w.
    ASSERT_TRUE(o.set_expression(ex_quat.get(), "0, 0, 0, 1"));
    EXPECT_EQ(o.get_value(ex_quat), (glm::quat{1.0f, 0.0f, 0.0f, 0.0f}));

    // Integer-vector target: per-component, rounded to nearest; a scalar
    // formula broadcasts, and an ivec3 source reads per component.
    o.set_value(ex_source, 2.6f);
    ASSERT_TRUE(o.set_expression(ex_ivec3.get(), "{ex_source}, 1.4, 0 - 1.6"));
    EXPECT_EQ(o.get_value(ex_ivec3), (glm::ivec3{3, 1, -2}));
    ASSERT_TRUE(o.set_expression(ex_ivec3.get(), "{ex_source}"));
    EXPECT_EQ(o.get_value(ex_ivec3), (glm::ivec3{3, 3, 3}));
    o.clear_value(ex_ivec3);
    o.set_value(ex_ivec3, glm::ivec3{10, 20, 30});
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_ivec3.y}"));
    EXPECT_EQ(o.get_value(ex_float), 20.0f);
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_bool} + {ex_mode}"));
    o.set_value(ex_source, 4.0f);

    // NaN is an error, the previous value stays (ex_bool is 1 now, so 5).
    EXPECT_EQ(o.get_value(ex_float), 5.0f);
    ASSERT_TRUE(o.set_expression(ex_float.get(), "sqrt(0 - {ex_source})"));
    EXPECT_FALSE(o.get_expression_error(ex_float.get()).empty());
    EXPECT_EQ(o.get_value(ex_float), 5.0f);
}

TEST(Expressions, result_goes_through_validate_and_coerce)
{
    Named_object o{"a"};
    o.set_value(ex_source, 5.0f);
    ASSERT_TRUE(o.set_expression(ex_clamped.get(), "{ex_source}"));
    EXPECT_EQ(o.get_value(ex_clamped), 1.0f);
    EXPECT_TRUE(o.is_coerced(ex_clamped.get()));
    EXPECT_EQ(o.read_local_value(ex_clamped).value(), 5.0f);
    o.set_value(ex_source, 0.25f);
    EXPECT_EQ(o.get_value(ex_clamped), 0.25f);
    EXPECT_FALSE(o.is_coerced(ex_clamped.get()));

    ASSERT_TRUE(o.set_expression(ex_positive.get(), "{ex_source} - 1"));
    EXPECT_FALSE(o.get_expression_error(ex_positive.get()).empty()); // -0.75 rejected
    EXPECT_EQ(o.get_value(ex_positive), 0.0f);
    o.set_value(ex_source, 3.0f);
    EXPECT_EQ(o.get_value(ex_positive), 2.0f);
    EXPECT_TRUE(o.get_expression_error(ex_positive.get()).empty());
}

TEST(Expressions, set_value_replaces_set_current_value_keeps_clear_drops)
{
    Named_object o{"a"};
    o.set_value(ex_source, 1.0f);
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_source} * 2"));
    EXPECT_EQ(o.get_value(ex_float), 2.0f);

    o.set_current_value(ex_float.get(), 100.0f);
    EXPECT_EQ(o.get_value(ex_float), 100.0f);
    EXPECT_TRUE(o.get_expression(ex_float.get()).has_value());
    EXPECT_EQ(o.get_value_source(ex_float.get()), Value_source::expression);
    o.set_value(ex_source, 2.0f);
    EXPECT_EQ(o.get_value(ex_float), 4.0f); // the expression is still driving

    o.set_value(ex_float, 50.0f);
    EXPECT_FALSE(o.get_expression(ex_float.get()).has_value());
    EXPECT_EQ(o.get_value_source(ex_float.get()), Value_source::local);
    o.set_value(ex_source, 3.0f);
    EXPECT_EQ(o.get_value(ex_float), 50.0f); // detached: the source no longer reaches it

    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_source}"));
    EXPECT_EQ(o.get_value(ex_float), 3.0f);
    o.clear_value(ex_float);
    EXPECT_FALSE(o.get_expression(ex_float.get()).has_value());
    EXPECT_FALSE(o.has_local_value(ex_float.get()));
    EXPECT_EQ(o.get_value_source(ex_float.get()), Value_source::default_value);
    o.set_value(ex_source, 4.0f);
    EXPECT_EQ(o.get_value(ex_float), 0.0f);

    // Replacing an expression drops the old dependents: only the new
    // source drives.
    Named_object other{"other"};
    other.set_value(ex_source, 10.0f);
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_source}"));
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{other/ex_source}"));
    EXPECT_EQ(o.get_value(ex_float), 10.0f);
    const std::size_t before = o.change_count("ex_float");
    o.set_value(ex_source, 5.0f);
    EXPECT_EQ(o.change_count("ex_float"), before);
    EXPECT_EQ(o.get_value(ex_float), 10.0f);
}

TEST(Expressions, local_state_round_trip)
{
    Named_object o{"a"};
    o.set_value(ex_source, 2.0f);

    EXPECT_FALSE(o.read_local_state(ex_float.get()).has_value());
    o.set_value(ex_float, 1.0f);
    const std::optional<Local_state> value_state = o.read_local_state(ex_float.get());
    ASSERT_TRUE(value_state.has_value());
    EXPECT_EQ(std::get<Property_value>(value_state.value()), Property_value{1.0f});

    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_source} + 1"));
    const std::optional<Local_state> expression_state = o.read_local_state(ex_float.get());
    ASSERT_TRUE(expression_state.has_value());
    EXPECT_EQ(std::get<Expression_text>(expression_state.value()).text, "{ex_source} + 1");

    o.apply_local_state(ex_float.get(), value_state);
    EXPECT_EQ(o.get_value(ex_float), 1.0f);
    EXPECT_FALSE(o.get_expression(ex_float.get()).has_value());
    o.apply_local_state(ex_float.get(), expression_state);
    EXPECT_EQ(o.get_value(ex_float), 3.0f);
    EXPECT_EQ(o.get_expression(ex_float.get()).value(), "{ex_source} + 1");
    o.apply_local_state(ex_float.get(), std::nullopt);
    EXPECT_FALSE(o.has_local_value(ex_float.get()));

    // Property_set bakes the result.
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_source} * 3"));
    const Property_set bag = Property_set::read_local_values(o);
    EXPECT_EQ(bag.find(ex_float.get()).value(), Property_value{6.0f});
}

TEST(Expressions, lazy_resolution_and_source_lifetime)
{
    Named_object o{"a"};
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{late/ex_source} + 1"));
    EXPECT_FALSE(o.get_expression_error(ex_float.get()).empty()); // unresolved
    EXPECT_EQ(o.get_value(ex_float), 0.0f);

    {
        Named_object late{"late"};
        late.set_value(ex_source, 41.0f);
        // Pull: the next read resolves and evaluates.
        EXPECT_EQ(o.get_value(ex_float), 42.0f);
        EXPECT_TRUE(o.get_expression_error(ex_float.get()).empty());
        // Push works from then on.
        late.set_value(ex_source, 1.0f);
        EXPECT_EQ(o.get_value(ex_float), 2.0f);
    }
    // The source is gone: the reference is unresolved again, the last value
    // stays, and nothing dangles.
    EXPECT_FALSE(o.get_expression_error(ex_float.get()).empty());
    EXPECT_EQ(o.get_value(ex_float), 2.0f);

    // A new object with that name is picked up.
    Named_object again{"late"};
    again.set_value(ex_source, 9.0f);
    EXPECT_EQ(o.get_value(ex_float), 10.0f);

    // Target destroyed before the source: the source's dependent list is
    // cleaned, so a later source change touches nothing.
    {
        Named_object target{"target"};
        ASSERT_TRUE(target.set_expression(ex_float.get(), "{late/ex_source}"));
        EXPECT_EQ(target.get_value(ex_float), 9.0f);
    }
    again.set_value(ex_source, 11.0f);
    EXPECT_EQ(o.get_value(ex_float), 12.0f);
}

TEST(Expressions, cycles_are_rejected_or_contained)
{
    Named_object a{"a"};
    Named_object b{"b"};
    ASSERT_TRUE(a.set_expression(ex_float.get(), "{b/ex_float} + 1"));
    // b.ex_float -> a.ex_float -> b.ex_float: rejected at attach, and the
    // previous state of b stays.
    b.set_value(ex_float, 5.0f);
    EXPECT_FALSE(b.set_expression(ex_float.get(), "{a/ex_float} * 2"));
    EXPECT_EQ(b.get_value(ex_float), 5.0f);
    EXPECT_EQ(b.get_value_source(ex_float.get()), Value_source::local);
    EXPECT_EQ(a.get_value(ex_float), 6.0f);

    // Same object, two properties: a.ex_int -> a.ex_float -> b.ex_float, then
    // b.ex_float -> a.ex_int would close the loop.
    ASSERT_TRUE(a.set_expression(ex_int.get(), "{ex_float}"));
    EXPECT_EQ(a.get_value(ex_int), 6);
    EXPECT_FALSE(b.set_expression(ex_float.get(), "{a/ex_int}"));

    // A cycle closed by lazy resolution is contained at evaluation: the
    // guard reports it and no value changes.
    Named_object c{"c"};
    ASSERT_TRUE(c.set_expression(ex_float.get(), "{d/ex_float} + 1")); // d does not exist yet
    Named_object d{"d"};
    ASSERT_TRUE(d.set_expression(ex_float.get(), "{c/ex_float} + 1")); // c/ex_float resolves, its own reference to d is still pending
    static_cast<void>(c.get_value(ex_float));
    static_cast<void>(d.get_value(ex_float));
    EXPECT_TRUE(!c.get_expression_error(ex_float.get()).empty() || !d.get_expression_error(ex_float.get()).empty());
    c.set_value(ex_source, 1.0f); // unrelated writes still terminate
    EXPECT_LT(c.get_value(ex_float), 100.0f);
}

TEST(Expressions, copies_carry_the_text_and_resolve_themselves)
{
    Named_object source{"src"};
    source.set_value(ex_source, 3.0f);
    Named_object original{"orig"};
    ASSERT_TRUE(original.set_expression(ex_float.get(), "{src/ex_source} * 2"));
    EXPECT_EQ(original.get_value(ex_float), 6.0f);

    Named_object copy{original};
    EXPECT_EQ(copy.get_expression(ex_float.get()).value(), "{src/ex_source} * 2");
    EXPECT_EQ(copy.get_value(ex_float), 6.0f); // resolved on read
    source.set_value(ex_source, 4.0f);
    EXPECT_EQ(original.get_value(ex_float), 8.0f);
    EXPECT_EQ(copy.get_value(ex_float), 8.0f);

    // Assignment replaces expressions and detaches the old ones.
    Named_object other{"other"};
    other.set_value(ex_float, 1.0f);
    copy = other;
    EXPECT_FALSE(copy.get_expression(ex_float.get()).has_value());
    source.set_value(ex_source, 5.0f);
    EXPECT_EQ(copy.get_value(ex_float), 1.0f);
    EXPECT_EQ(original.get_value(ex_float), 10.0f);
}

TEST(Expressions, bridged_target_and_invalidate_dependents)
{
    Bridged_target t{"t"};
    Named_object   driver{"driver"};
    driver.set_value(ex_vec3, glm::vec3{1.0f, 2.0f, 3.0f});

    ASSERT_TRUE(t.set_expression(bridged_target_position.get(), "{driver/ex_vec3} + 1"));
    EXPECT_EQ(t.position, (glm::vec3{2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(t.set_calls, 1);
    EXPECT_EQ(t.get_value(bridged_target_position), (glm::vec3{2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(t.get_value_source(bridged_target_position.get()), Value_source::expression);
    EXPECT_EQ(t.read_local_value(bridged_target_position).value(), (glm::vec3{2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(t.change_count("bridged_target_position"), std::size_t{1});

    driver.set_value(ex_vec3, glm::vec3{0.0f});
    EXPECT_EQ(t.position, (glm::vec3{1.0f}));

    // for_each_local_value lists the bridged property once.
    int listed = 0;
    t.for_each_local_value([&](const Dependency_property& p, const Property_value&) { if (&p == &bridged_target_position.get()) { ++listed; } });
    EXPECT_EQ(listed, 1);

    // A value write replaces the expression; the bridge keeps the storage.
    t.set_value(bridged_target_position, glm::vec3{9.0f});
    EXPECT_FALSE(t.get_expression(bridged_target_position.get()).has_value());
    EXPECT_EQ(t.get_value_source(bridged_target_position.get()), Value_source::local);
    driver.set_value(ex_vec3, glm::vec3{5.0f});
    EXPECT_EQ(t.position, (glm::vec3{9.0f}));

    // A bridged SOURCE written behind the bridge: the owner announces it
    // with invalidate_dependents.
    Bridged_target s{"s"};
    ASSERT_TRUE(t.set_expression(bridged_target_scalar.get(), "{s/bridged_target_position.y}"));
    EXPECT_EQ(t.get_value(bridged_target_scalar), 0.0f);
    s.position.y = 7.0f;
    EXPECT_EQ(t.get_value(bridged_target_scalar), 0.0f); // nobody told anyone
    s.invalidate_dependents(bridged_target_position.get());
    EXPECT_EQ(t.get_value(bridged_target_scalar), 7.0f);
    EXPECT_EQ(t.change_count("bridged_target_scalar"), std::size_t{2});

    // Clearing a bridged driven property writes the default and drops the
    // expression.
    t.clear_value(bridged_target_position);
    EXPECT_FALSE(t.get_expression(bridged_target_position.get()).has_value());
}

TEST(Expressions, batches_collapse_before_dependents_run)
{
    Named_object o{"a"};
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_source} + {ex_other.x}"));
    EXPECT_EQ(o.get_value(ex_float), 2.0f); // ex_source defaults to 2
    const std::size_t before = o.change_count("ex_float");
    {
        const Dependency_object::Change_batch batch{o};
        o.set_value(ex_source, 1.0f);
        o.set_value(ex_source, 3.0f);
        o.set_value(ex_other, glm::vec3{4.0f, 0.0f, 0.0f});
        EXPECT_EQ(o.change_count("ex_float"), before); // nothing delivered yet
        EXPECT_EQ(o.get_value(ex_float), 2.0f);
    }
    EXPECT_EQ(o.get_value(ex_float), 7.0f);
    // The batch stores values at once and defers notifications, so the
    // first delivered source change (ex_source, its two writes collapsed)
    // already evaluates to the final result and the second (ex_other)
    // re-evaluates to the same value: one notification, 2 -> 7.
    EXPECT_EQ(o.change_count("ex_float"), before + 1);
    const auto last = std::find_if(o.changes.rbegin(), o.changes.rend(), [](const Recorded_change& c) { return c.property_name == "ex_float"; });
    ASSERT_NE(last, o.changes.rend());
    EXPECT_EQ(std::get<float>(last->old_value), 2.0f);
    EXPECT_EQ(std::get<float>(last->new_value), 7.0f);
}

// M6 value types: a double is a one-component numeric target and source; a
// mat4 is not expressible at all.
TEST(Expressions, double_is_a_numeric_target_and_source)
{
    Named_object o{"double_host"};
    o.set_value(ex_source, 2.0f);

    ASSERT_TRUE(o.set_expression(ex_double.get(), "{ex_source} / 3"));
    EXPECT_DOUBLE_EQ(o.get_value(ex_double), 2.0 / 3.0);

    // The double result keeps precision a float target would lose.
    EXPECT_NE(static_cast<float>(o.get_value(ex_double)), o.get_value(ex_double));

    // A double property reads back as a number in another formula.
    o.clear_value(ex_double);
    o.set_value(ex_double, 0.5);
    ASSERT_TRUE(o.set_expression(ex_float.get(), "{ex_double} * 4"));
    EXPECT_EQ(o.get_value(ex_float), 2.0f);
}

TEST(Expressions, mat4_is_refused_as_an_expression_target)
{
    EXPECT_EQ(Expression::component_count(Property_type::mat4), 0);

    std::string error;
    EXPECT_EQ(Expression::compile("1", Property_type::mat4, error), nullptr);
    EXPECT_NE(error.find("mat4"), std::string::npos);
    EXPECT_NE(error.find("cannot be driven by an expression"), std::string::npos);

    Named_object o{"mat4_host"};
    EXPECT_FALSE(o.set_expression(ex_mat4.get(), "1"));
    EXPECT_FALSE(o.get_expression(ex_mat4.get()).has_value());
}

// M6 value types: an asset path and the array types are whole values, so
// none of them is an expression target or an expression source.
TEST(Expressions, asset_path_and_arrays_are_refused_as_expression_targets)
{
    EXPECT_EQ(Expression::component_count(Property_type::asset_path), 0);
    EXPECT_EQ(Expression::component_count(Property_type::float_array), 0);
    EXPECT_EQ(Expression::component_count(Property_type::int_array), 0);

    const Property_type types[3] = {Property_type::asset_path, Property_type::float_array, Property_type::int_array};
    const char* const  names[3] = {"asset", "float[]", "int[]"};
    for (int index = 0; index < 3; ++index) {
        std::string error;
        EXPECT_EQ(Expression::compile("1", types[index], error), nullptr);
        EXPECT_NE(error.find(names[index]), std::string::npos);
        EXPECT_NE(error.find("cannot be driven by an expression"), std::string::npos);
    }

    Named_object o{"array_host"};
    EXPECT_FALSE(o.set_expression(ex_asset.get(), "1"));
    EXPECT_FALSE(o.set_expression(ex_floats.get(), "1"));
    EXPECT_FALSE(o.set_expression(ex_ints.get(), "1"));
    EXPECT_FALSE(o.get_expression(ex_asset.get()).has_value());
    EXPECT_FALSE(o.get_expression(ex_floats.get()).has_value());
    EXPECT_FALSE(o.get_expression(ex_ints.get()).has_value());

    // Nor is one of them a source another formula can read a component of.
    o.set_value(ex_floats, std::vector<float>{1.0f, 2.0f});
    EXPECT_TRUE(o.set_expression(ex_float.get(), "{ex_floats}"));
    EXPECT_NE(o.get_expression_error(ex_float.get()).find("is an array property"), std::string_view::npos);
}
