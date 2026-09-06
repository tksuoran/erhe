// Style layer (D25 in doc/property-system.md): coerced > local >
// style > inherited > default; set_style notifies the changed non-local
// properties; a style value is inherited by descendants.

#include "test_object.hpp"

#include "erhe_property/property_style.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

const Property<float> st_a   = Property<float>::register_property("st_a", type_a(), Property_metadata{.default_value = 1.0f});
const Property<float> st_b   = Property<float>::register_property("st_b", type_a(), Property_metadata{.default_value = 2.0f});
const Property<float> st_inh = Property<float>::register_property("st_inh", type_a(), Property_metadata{.default_value = 0.0f, .inherits = true});

auto make_style(const std::string_view name, const float a, const std::optional<float> b = {}, const std::optional<float> inh = {}) -> std::shared_ptr<const Property_style>
{
    Property_set values;
    values.set(st_a.get(), Property_value{a});
    if (b.has_value()) {
        values.set(st_b.get(), Property_value{b.value()});
    }
    if (inh.has_value()) {
        values.set(st_inh.get(), Property_value{inh.value()});
    }
    return std::make_shared<const Property_style>(std::string{name}, std::move(values));
}

} // anonymous namespace

TEST(Style, precedence)
{
    Test_object o;
    EXPECT_TRUE(o.set_style(make_style("s", 10.0f, 20.0f)));
    EXPECT_EQ(o.get_value(st_a), 10.0f);
    EXPECT_EQ(o.get_value_source(st_a.get()), Value_source::style);
    EXPECT_FALSE(o.has_local_value(st_a.get()));
    EXPECT_FALSE(o.read_local_value(st_a).has_value());

    o.set_value(st_a, 5.0f); // local wins
    EXPECT_EQ(o.get_value(st_a), 5.0f);
    EXPECT_EQ(o.get_value_source(st_a.get()), Value_source::local);
    o.clear_value(st_a); // back to the style, not the default
    EXPECT_EQ(o.get_value(st_a), 10.0f);
    EXPECT_EQ(o.get_value_source(st_a.get()), Value_source::style);

    ASSERT_TRUE(o.set_expression(st_b.get(), "7")); // expression over style
    EXPECT_EQ(o.get_value(st_b), 7.0f);
    EXPECT_EQ(o.get_value_source(st_b.get()), Value_source::expression);
}

TEST(Style, set_style_notifies_changed_non_local_properties)
{
    Test_object o;
    o.set_value(st_b, 3.0f);
    o.changes.clear();

    ASSERT_TRUE(o.set_style(make_style("s1", 10.0f, 20.0f)));
    ASSERT_EQ(o.changes.size(), std::size_t{1}); // st_b is local: untouched
    EXPECT_EQ(o.changes[0].property_name, "st_a");
    EXPECT_EQ(std::get<float>(o.changes[0].old_value), 1.0f);
    EXPECT_EQ(std::get<float>(o.changes[0].new_value), 10.0f);
    EXPECT_EQ(o.changes[0].old_source, Value_source::default_value);
    EXPECT_EQ(o.changes[0].new_source, Value_source::style);
    EXPECT_EQ(o.get_value(st_b), 3.0f);

    // Swap: same value for st_a in the new style -> no notification for it.
    o.changes.clear();
    ASSERT_TRUE(o.set_style(make_style("s2", 10.0f)));
    EXPECT_TRUE(o.changes.empty());

    o.changes.clear();
    ASSERT_TRUE(o.set_style(make_style("s3", 11.0f)));
    ASSERT_EQ(o.changes.size(), std::size_t{1});
    EXPECT_EQ(o.changes[0].old_source, Value_source::style);
    EXPECT_EQ(o.changes[0].new_source, Value_source::style);
    EXPECT_EQ(std::get<float>(o.changes[0].new_value), 11.0f);

    // Clear: back to the default.
    o.changes.clear();
    ASSERT_TRUE(o.set_style(nullptr));
    ASSERT_EQ(o.changes.size(), std::size_t{1});
    EXPECT_EQ(std::get<float>(o.changes[0].new_value), 1.0f);
    EXPECT_EQ(o.changes[0].new_source, Value_source::default_value);
    EXPECT_FALSE(o.get_style());
}

TEST(Style, style_value_is_inherited_and_shadows_ancestors)
{
    Test_object root;
    Test_object mid;
    Test_object leaf;
    mid.set_parent(&root);
    leaf.set_parent(&mid);
    root.set_value(st_inh, 1.0f);
    leaf.changes.clear();

    ASSERT_TRUE(mid.set_style(make_style("s", 0.0f, {}, 5.0f)));
    EXPECT_EQ(mid.get_value(st_inh), 5.0f);
    EXPECT_EQ(mid.get_value_source(st_inh.get()), Value_source::style);
    EXPECT_EQ(leaf.get_value(st_inh), 5.0f);
    EXPECT_EQ(leaf.get_value_source(st_inh.get()), Value_source::inherited);
    EXPECT_EQ(leaf.change_count("st_inh"), std::size_t{1});

    // The ancestor's change stops at the styled child.
    leaf.changes.clear();
    root.set_value(st_inh, 2.0f);
    EXPECT_EQ(leaf.get_value(st_inh), 5.0f);
    EXPECT_EQ(leaf.change_count("st_inh"), std::size_t{0});

    // Reparenting the styled object keeps its value; its child follows it.
    Test_object other_root;
    other_root.set_value(st_inh, 9.0f);
    mid.set_parent(&other_root);
    EXPECT_EQ(mid.get_value(st_inh), 5.0f);
    EXPECT_EQ(leaf.get_value(st_inh), 5.0f);
    EXPECT_EQ(leaf.change_count("st_inh"), std::size_t{0});
}

TEST(Style, copy_carries_style_and_sealed_rejects)
{
    Test_object o;
    const std::shared_ptr<const Property_style> style = make_style("s", 10.0f);
    ASSERT_TRUE(o.set_style(style));

    Test_object copy{o};
    EXPECT_EQ(copy.get_style(), style);
    EXPECT_EQ(copy.get_value(st_a), 10.0f);

    o.seal();
    EXPECT_FALSE(o.set_style(nullptr));
    EXPECT_EQ(o.get_style(), style);
    o.unseal();
    EXPECT_TRUE(o.set_style(nullptr));
}

TEST(Style, source_edit_reaches_users_live)
{
    // D25 live edit (doc/style-library.md D1): a change of the source's
    // local layer notifies every user reading the style for that property.
    std::shared_ptr<Property_style> style = std::make_shared<Property_style>("s", Property_set{});
    Test_object a;
    Test_object b;
    EXPECT_TRUE(a.set_style(style));
    EXPECT_TRUE(b.set_style(style));
    EXPECT_EQ(style->get_style_user_count(), std::size_t{2});
    b.set_value(st_a, 5.0f); // b shadows the style for st_a

    a.changes.clear();
    b.changes.clear();
    style->set_value(st_a, 2.0f);
    EXPECT_EQ(a.get_value(st_a), 2.0f);
    EXPECT_EQ(a.get_value_source(st_a.get()), Value_source::style);
    ASSERT_EQ(a.changes.size(), std::size_t{1});
    EXPECT_EQ(a.changes[0].old_source, Value_source::default_value);
    EXPECT_EQ(a.changes[0].new_source, Value_source::style);
    EXPECT_TRUE(b.changes.empty()); // local value: untouched

    style->set_value(st_a, 3.0f);
    ASSERT_EQ(a.changes.size(), std::size_t{2});
    EXPECT_EQ(std::get<float>(a.changes[1].old_value), 2.0f);
    EXPECT_EQ(std::get<float>(a.changes[1].new_value), 3.0f);

    style->clear_value(st_a);
    ASSERT_EQ(a.changes.size(), std::size_t{3});
    EXPECT_EQ(a.changes[2].new_source, Value_source::default_value);
    EXPECT_EQ(a.get_value(st_a), 1.0f); // st_a's registered default

    // A user that leaves the style stops being notified; a destroyed user
    // leaves the source's list.
    EXPECT_TRUE(a.set_style(nullptr));
    EXPECT_EQ(style->get_style_user_count(), std::size_t{1});
    {
        Test_object c;
        EXPECT_TRUE(c.set_style(style));
        EXPECT_EQ(style->get_style_user_count(), std::size_t{2});
    }
    EXPECT_EQ(style->get_style_user_count(), std::size_t{1});
    style->set_value(st_a, 4.0f);
    EXPECT_EQ(a.changes.size(), std::size_t{3});
}

TEST(Style, chain_resolves_and_local_shadows)
{
    // D25 style chain: an object reads its style's local value, else that
    // style's style's local value, and so on; a nearer local value shadows a
    // farther one.
    std::shared_ptr<Property_style> a = std::make_shared<Property_style>("a", Property_set{});
    std::shared_ptr<Property_style> b = std::make_shared<Property_style>("b", Property_set{});
    Test_object o;
    a->set_value(st_a, 10.0f);
    a->set_value(st_b, 20.0f);
    b->set_value(st_b, 30.0f);
    ASSERT_TRUE(b->set_style(a));
    ASSERT_TRUE(o.set_style(b));

    EXPECT_EQ(o.get_value(st_a), 10.0f); // two levels deep: from a, through b
    EXPECT_EQ(o.get_value_source(st_a.get()), Value_source::style);
    EXPECT_EQ(o.get_value(st_b), 30.0f); // b's own local shadows a's
    EXPECT_EQ(o.get_value_source(st_b.get()), Value_source::style);

    o.set_value(st_a, 5.0f); // the object's own local shadows the whole chain
    EXPECT_EQ(o.get_value(st_a), 5.0f);
    EXPECT_EQ(o.get_value_source(st_a.get()), Value_source::local);
    o.clear_value(st_a);
    EXPECT_EQ(o.get_value(st_a), 10.0f);

}

TEST(Style, style_inherited_value_is_not_style)
{
    // Only the LOCAL values of the styles on the chain are style: a value a
    // style itself inherits from its own tree stays out of it.
    Test_object parent;
    std::shared_ptr<Test_object> style = std::make_shared<Test_object>();
    parent.set_value(st_inh, 7.0f);
    style->set_parent(&parent);
    EXPECT_EQ(style->get_value(st_inh), 7.0f);

    Test_object o;
    ASSERT_TRUE(o.set_style(style));
    EXPECT_EQ(o.get_value(st_inh), 0.0f); // st_inh's registered default
    EXPECT_EQ(o.get_value_source(st_inh.get()), Value_source::default_value);
    EXPECT_TRUE(o.set_style(nullptr));
}

TEST(Style, chain_edit_reaches_users_live)
{
    // A local edit on the far end of the chain reaches an object whose style
    // is the near end, with the object's own old and new values.
    std::shared_ptr<Property_style> a = std::make_shared<Property_style>("a", Property_set{});
    std::shared_ptr<Property_style> b = std::make_shared<Property_style>("b", Property_set{});
    ASSERT_TRUE(b->set_style(a));
    Test_object o;
    ASSERT_TRUE(o.set_style(b));

    o.changes.clear();
    a->set_value(st_a, 2.0f);
    EXPECT_EQ(o.get_value(st_a), 2.0f);
    ASSERT_EQ(o.changes.size(), std::size_t{1});
    EXPECT_EQ(o.changes[0].old_source, Value_source::default_value);
    EXPECT_EQ(std::get<float>(o.changes[0].old_value), 1.0f); // st_a's default
    EXPECT_EQ(o.changes[0].new_source, Value_source::style);
    EXPECT_EQ(std::get<float>(o.changes[0].new_value), 2.0f);

    a->set_value(st_a, 3.0f);
    ASSERT_EQ(o.changes.size(), std::size_t{2});
    EXPECT_EQ(std::get<float>(o.changes[1].old_value), 2.0f);
    EXPECT_EQ(std::get<float>(o.changes[1].new_value), 3.0f);

    // b's own local shadows a for the object, and a's edits stop reaching it.
    b->set_value(st_a, 4.0f);
    EXPECT_EQ(o.get_value(st_a), 4.0f);
    o.changes.clear();
    a->set_value(st_a, 9.0f);
    EXPECT_EQ(o.get_value(st_a), 4.0f);
    EXPECT_TRUE(o.changes.empty());

    // Clearing b's local lets a through again, live.
    b->clear_value(st_a);
    EXPECT_EQ(o.get_value(st_a), 9.0f);
    ASSERT_EQ(o.changes.size(), std::size_t{1});
    EXPECT_EQ(std::get<float>(o.changes[0].old_value), 4.0f);
    EXPECT_EQ(std::get<float>(o.changes[0].new_value), 9.0f);
}

TEST(Style, cycle_is_refused)
{
    // set_style refuses a source whose chain reaches the object itself, so a
    // style chain never cycles.
    std::shared_ptr<Property_style> a = std::make_shared<Property_style>("a", Property_set{});
    std::shared_ptr<Property_style> b = std::make_shared<Property_style>("b", Property_set{});
    std::shared_ptr<Property_style> c = std::make_shared<Property_style>("c", Property_set{});
    EXPECT_FALSE(a->set_style(a));  // itself
    EXPECT_EQ(a->get_style(), nullptr);

    ASSERT_TRUE(b->set_style(a));
    EXPECT_FALSE(a->set_style(b));  // a -> b -> a
    EXPECT_EQ(a->get_style(), nullptr);
    EXPECT_EQ(b->get_style(), a);

    ASSERT_TRUE(c->set_style(b));
    EXPECT_FALSE(a->set_style(c));  // a -> c -> b -> a
    EXPECT_EQ(a->get_style(), nullptr);

    a->set_value(st_a, 8.0f);
    Test_object o;
    ASSERT_TRUE(o.set_style(c));
    EXPECT_EQ(o.get_value(st_a), 8.0f); // three levels: c -> b -> a
}
