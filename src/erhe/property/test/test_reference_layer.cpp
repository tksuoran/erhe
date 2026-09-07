// Reference layer (D33 in doc/property-system.md): coerced > local >
// style > reference > inherited > default. The reference layer of an
// object is what its counterpart supplies itself (local, expression,
// computed, style or, recursively, reference); a value the counterpart
// would inherit from its own tree is not carried.

#include "test_object.hpp"

#include "erhe_property/property_set.hpp"
#include "erhe_property/property_style.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

const Property<float> rf_a   = Property<float>::register_property("rf_a", type_a(), Property_metadata{.default_value = 1.0f});
const Property<float> rf_b   = Property<float>::register_property("rf_b", type_a(), Property_metadata{.default_value = 2.0f});
const Property<float> rf_inh = Property<float>::register_property("rf_inh", type_a(), Property_metadata{.default_value = 0.0f, .inherits = true});

auto make_style(const std::string_view name, const float inh) -> std::shared_ptr<const Property_style>
{
    Property_set values;
    values.set(rf_inh.get(), Property_value{inh});
    return std::make_shared<const Property_style>(std::string{name}, std::move(values));
}

} // anonymous namespace

TEST(Reference_layer, precedence)
{
    // All five layers on one property, peeled off one at a time.
    Test_object parent;
    Test_object object;
    std::shared_ptr<Test_object> counterpart = std::make_shared<Test_object>();
    object.set_parent(&parent);
    ASSERT_TRUE(object.set_reference(counterpart));

    EXPECT_EQ(object.get_value(rf_inh), 0.0f); // rf_inh's registered default
    EXPECT_EQ(object.get_value_source(rf_inh.get()), Value_source::default_value);

    parent.set_value(rf_inh, 3.0f);
    EXPECT_EQ(object.get_value(rf_inh), 3.0f);
    EXPECT_EQ(object.get_value_source(rf_inh.get()), Value_source::inherited);

    counterpart->set_value(rf_inh, 5.0f);
    EXPECT_EQ(object.get_value(rf_inh), 5.0f);
    EXPECT_EQ(object.get_value_source(rf_inh.get()), Value_source::reference);

    ASSERT_TRUE(object.set_style(make_style("s", 7.0f)));
    EXPECT_EQ(object.get_value(rf_inh), 7.0f);
    EXPECT_EQ(object.get_value_source(rf_inh.get()), Value_source::style);

    object.set_value(rf_inh, 9.0f);
    EXPECT_EQ(object.get_value(rf_inh), 9.0f);
    EXPECT_EQ(object.get_value_source(rf_inh.get()), Value_source::local);

    object.clear_value(rf_inh);
    EXPECT_EQ(object.get_value(rf_inh), 7.0f);
    EXPECT_EQ(object.get_value_source(rf_inh.get()), Value_source::style);

    ASSERT_TRUE(object.set_style(nullptr));
    EXPECT_EQ(object.get_value(rf_inh), 5.0f);
    EXPECT_EQ(object.get_value_source(rf_inh.get()), Value_source::reference);

    counterpart->clear_value(rf_inh);
    EXPECT_EQ(object.get_value(rf_inh), 3.0f);
    EXPECT_EQ(object.get_value_source(rf_inh.get()), Value_source::inherited);

    ASSERT_TRUE(object.set_reference(nullptr));
    parent.clear_value(rf_inh);
    EXPECT_EQ(object.get_value(rf_inh), 0.0f);
    EXPECT_EQ(object.get_value_source(rf_inh.get()), Value_source::default_value);
}

TEST(Reference_layer, counterpart_inherited_value_is_not_carried)
{
    // A reference arc composes the target's own opinions, not those of its
    // ancestors: the user's own tree provides inheritance.
    Test_object counterpart_parent;
    std::shared_ptr<Test_object> counterpart = std::make_shared<Test_object>();
    counterpart->set_parent(&counterpart_parent);
    counterpart_parent.set_value(rf_inh, 9.0f);
    EXPECT_EQ(counterpart->get_value(rf_inh), 9.0f);
    EXPECT_EQ(counterpart->get_value_source(rf_inh.get()), Value_source::inherited);

    Test_object object;
    ASSERT_TRUE(object.set_reference(counterpart));
    EXPECT_EQ(object.get_value(rf_inh), 0.0f); // its own default, not the counterpart's inherited value
    EXPECT_EQ(object.get_value_source(rf_inh.get()), Value_source::default_value);

    // The user's own tree supplies inheritance instead.
    Test_object parent;
    parent.set_value(rf_inh, 4.0f);
    object.set_parent(&parent);
    EXPECT_EQ(object.get_value(rf_inh), 4.0f);
    EXPECT_EQ(object.get_value_source(rf_inh.get()), Value_source::inherited);
    ASSERT_TRUE(object.set_reference(nullptr));
}

TEST(Reference_layer, set_reference_notifies_and_nullptr_clears)
{
    std::shared_ptr<Test_object> counterpart = std::make_shared<Test_object>();
    counterpart->set_value(rf_a, 10.0f);
    counterpart->set_value(rf_b, 20.0f);

    Test_object object;
    object.set_value(rf_b, 3.0f); // local: shadows the reference
    object.changes.clear();

    ASSERT_TRUE(object.set_reference(counterpart));
    ASSERT_EQ(object.changes.size(), std::size_t{1});
    EXPECT_EQ(object.changes[0].property_name, "rf_a");
    EXPECT_EQ(std::get<float>(object.changes[0].old_value), 1.0f);
    EXPECT_EQ(std::get<float>(object.changes[0].new_value), 10.0f);
    EXPECT_EQ(object.changes[0].old_source, Value_source::default_value);
    EXPECT_EQ(object.changes[0].new_source, Value_source::reference);
    EXPECT_EQ(object.get_value(rf_b), 3.0f);

    object.changes.clear();
    ASSERT_TRUE(object.set_reference(nullptr));
    ASSERT_EQ(object.changes.size(), std::size_t{1});
    EXPECT_EQ(object.changes[0].property_name, "rf_a");
    EXPECT_EQ(std::get<float>(object.changes[0].new_value), 1.0f);
    EXPECT_EQ(object.changes[0].new_source, Value_source::default_value);
    EXPECT_FALSE(object.get_reference());
}

TEST(Reference_layer, counterpart_edit_reaches_users_live)
{
    std::shared_ptr<Test_object> counterpart = std::make_shared<Test_object>();
    Test_object a;
    Test_object b;
    ASSERT_TRUE(a.set_reference(counterpart));
    ASSERT_TRUE(b.set_reference(counterpart));
    EXPECT_EQ(counterpart->get_reference_user_count(), std::size_t{2});
    b.set_value(rf_a, 5.0f); // b overrides the reference for rf_a

    a.changes.clear();
    b.changes.clear();
    counterpart->set_value(rf_a, 2.0f);
    EXPECT_EQ(a.get_value(rf_a), 2.0f);
    EXPECT_EQ(a.get_value_source(rf_a.get()), Value_source::reference);
    ASSERT_EQ(a.changes.size(), std::size_t{1});
    EXPECT_EQ(std::get<float>(a.changes[0].old_value), 1.0f);
    EXPECT_EQ(a.changes[0].old_source, Value_source::default_value);
    EXPECT_EQ(a.changes[0].new_source, Value_source::reference);
    EXPECT_TRUE(b.changes.empty()); // local override: untouched
    EXPECT_EQ(b.get_value(rf_a), 5.0f);

    counterpart->set_value(rf_a, 3.0f);
    ASSERT_EQ(a.changes.size(), std::size_t{2});
    EXPECT_EQ(std::get<float>(a.changes[1].old_value), 2.0f);
    EXPECT_EQ(a.changes[1].old_source, Value_source::reference);
    EXPECT_EQ(std::get<float>(a.changes[1].new_value), 3.0f);
    EXPECT_EQ(a.changes[1].new_source, Value_source::reference);

    // The user's own local shadows the reference; clearing it exposes the
    // reference value again.
    a.set_value(rf_a, 8.0f);
    EXPECT_EQ(a.get_value_source(rf_a.get()), Value_source::local);
    a.changes.clear();
    counterpart->set_value(rf_a, 4.0f);
    EXPECT_EQ(a.get_value(rf_a), 8.0f);
    EXPECT_TRUE(a.changes.empty());
    a.clear_value(rf_a);
    EXPECT_EQ(a.get_value(rf_a), 4.0f);
    EXPECT_EQ(a.get_value_source(rf_a.get()), Value_source::reference);

    // A user that leaves the counterpart stops being notified; a destroyed
    // user leaves the source's list.
    ASSERT_TRUE(a.set_reference(nullptr));
    EXPECT_EQ(counterpart->get_reference_user_count(), std::size_t{1});
    {
        Test_object c;
        ASSERT_TRUE(c.set_reference(counterpart));
        EXPECT_EQ(counterpart->get_reference_user_count(), std::size_t{2});
    }
    EXPECT_EQ(counterpart->get_reference_user_count(), std::size_t{1});
}

TEST(Reference_layer, counterpart_clear_exposes_the_users_inherited_value)
{
    Test_object parent;
    Test_object object;
    object.set_parent(&parent);
    parent.set_value(rf_inh, 7.0f);
    std::shared_ptr<Test_object> counterpart = std::make_shared<Test_object>();
    counterpart->set_value(rf_inh, 5.0f);
    ASSERT_TRUE(object.set_reference(counterpart));
    EXPECT_EQ(object.get_value(rf_inh), 5.0f);

    object.changes.clear();
    counterpart->clear_value(rf_inh);
    EXPECT_EQ(object.get_value(rf_inh), 7.0f);
    EXPECT_EQ(object.get_value_source(rf_inh.get()), Value_source::inherited);
    ASSERT_EQ(object.changes.size(), std::size_t{1});
    EXPECT_EQ(std::get<float>(object.changes[0].old_value), 5.0f);
    EXPECT_EQ(object.changes[0].old_source, Value_source::reference);
    EXPECT_EQ(std::get<float>(object.changes[0].new_value), 7.0f);
    EXPECT_EQ(object.changes[0].new_source, Value_source::inherited);
    ASSERT_TRUE(object.set_reference(nullptr));
}

TEST(Reference_layer, chain_resolves_and_edits_reach_the_far_end)
{
    // A refs B refs C: what B supplies includes what B's own reference
    // supplies, so C's value reaches A.
    std::shared_ptr<Test_object> c = std::make_shared<Test_object>();
    std::shared_ptr<Test_object> b = std::make_shared<Test_object>();
    Test_object a;
    c->set_value(rf_a, 10.0f);
    c->set_value(rf_b, 20.0f);
    b->set_value(rf_b, 30.0f);
    ASSERT_TRUE(b->set_reference(c));
    ASSERT_TRUE(a.set_reference(b));

    EXPECT_EQ(a.get_value(rf_a), 10.0f); // two levels deep: from c, through b
    EXPECT_EQ(a.get_value_source(rf_a.get()), Value_source::reference);
    EXPECT_EQ(a.get_value(rf_b), 30.0f); // b's own local shadows c's
    EXPECT_EQ(a.get_value_source(rf_b.get()), Value_source::reference);

    a.changes.clear();
    c->set_value(rf_a, 11.0f);
    EXPECT_EQ(a.get_value(rf_a), 11.0f);
    ASSERT_EQ(a.changes.size(), std::size_t{1});
    EXPECT_EQ(std::get<float>(a.changes[0].old_value), 10.0f);
    EXPECT_EQ(std::get<float>(a.changes[0].new_value), 11.0f);
    EXPECT_EQ(a.changes[0].new_source, Value_source::reference);

    ASSERT_TRUE(a.set_reference(nullptr));
}

TEST(Reference_layer, cycle_is_refused_and_sealed_rejects)
{
    std::shared_ptr<Test_object> a = std::make_shared<Test_object>();
    std::shared_ptr<Test_object> b = std::make_shared<Test_object>();
    std::shared_ptr<Test_object> c = std::make_shared<Test_object>();
    EXPECT_FALSE(a->set_reference(a)); // itself
    EXPECT_EQ(a->get_reference(), nullptr);

    ASSERT_TRUE(b->set_reference(a));
    EXPECT_FALSE(a->set_reference(b)); // a -> b -> a
    EXPECT_EQ(a->get_reference(), nullptr);

    ASSERT_TRUE(c->set_reference(b));
    EXPECT_FALSE(a->set_reference(c)); // a -> c -> b -> a
    EXPECT_EQ(a->get_reference(), nullptr);
    EXPECT_TRUE(c->reference_chain_reaches(*a));
    EXPECT_FALSE(a->reference_chain_reaches(*c));

    Test_object sealed_object;
    sealed_object.seal();
    EXPECT_FALSE(sealed_object.set_reference(a));
    EXPECT_EQ(sealed_object.get_reference(), nullptr);
    sealed_object.unseal();
    EXPECT_TRUE(sealed_object.set_reference(a));
    EXPECT_TRUE(sealed_object.set_reference(nullptr));

    ASSERT_TRUE(c->set_reference(nullptr));
    ASSERT_TRUE(b->set_reference(nullptr));
}

TEST(Reference_layer, copy_carries_the_reference_and_registers_as_a_user)
{
    std::shared_ptr<Test_object> counterpart = std::make_shared<Test_object>();
    counterpart->set_value(rf_a, 10.0f);
    Test_object object;
    ASSERT_TRUE(object.set_reference(counterpart));
    EXPECT_EQ(counterpart->get_reference_user_count(), std::size_t{1});

    {
        Test_object copy{object};
        EXPECT_EQ(copy.get_reference(), counterpart);
        EXPECT_EQ(copy.get_value(rf_a), 10.0f);
        EXPECT_EQ(copy.get_value_source(rf_a.get()), Value_source::reference);
        EXPECT_EQ(counterpart->get_reference_user_count(), std::size_t{2});

        copy.changes.clear();
        counterpart->set_value(rf_a, 12.0f);
        EXPECT_EQ(copy.get_value(rf_a), 12.0f);
        EXPECT_EQ(copy.change_count("rf_a"), std::size_t{1});
    }
    EXPECT_EQ(counterpart->get_reference_user_count(), std::size_t{1});
    ASSERT_TRUE(object.set_reference(nullptr));
}

TEST(Reference_layer, instance_ancestor_override_reaches_an_instance_descendant)
{
    // The instance mirrors the template's structure. The template's child
    // has no own value for the property, so the instance's child falls to
    // its OWN tree and reads the override its instance root holds - which
    // is why the counterpart's inherited value is not carried.
    Test_object template_root;
    std::shared_ptr<Test_object> template_child = std::make_shared<Test_object>();
    std::shared_ptr<Test_object> template_root_reference = std::make_shared<Test_object>();
    template_child->set_parent(&template_root);
    template_root.set_value(rf_inh, 2.0f);
    template_root_reference->set_value(rf_inh, 2.0f);
    EXPECT_EQ(template_child->get_value(rf_inh), 2.0f);
    EXPECT_EQ(template_child->get_value_source(rf_inh.get()), Value_source::inherited);

    Test_object instance_root;
    Test_object instance_child;
    instance_child.set_parent(&instance_root);
    ASSERT_TRUE(instance_root.set_reference(template_root_reference));
    ASSERT_TRUE(instance_child.set_reference(template_child));
    EXPECT_EQ(instance_root.get_value(rf_inh), 2.0f);
    EXPECT_EQ(instance_root.get_value_source(rf_inh.get()), Value_source::reference);
    EXPECT_EQ(instance_child.get_value(rf_inh), 2.0f);
    EXPECT_EQ(instance_child.get_value_source(rf_inh.get()), Value_source::inherited);

    instance_child.changes.clear();
    instance_root.set_value(rf_inh, 6.0f); // the override on the instance ancestor
    EXPECT_EQ(instance_root.get_value_source(rf_inh.get()), Value_source::local);
    EXPECT_EQ(instance_child.get_value(rf_inh), 6.0f);
    EXPECT_EQ(instance_child.get_value_source(rf_inh.get()), Value_source::inherited);
    EXPECT_EQ(instance_child.change_count("rf_inh"), std::size_t{1});

    ASSERT_TRUE(instance_child.set_reference(nullptr));
    ASSERT_TRUE(instance_root.set_reference(nullptr));
}
