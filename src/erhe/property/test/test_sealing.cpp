// Sealing (D24 in doc/property_system.md): while sealed, every write
// of the local layer is rejected; reads, inheritance, observers and an
// installed expression keep working; a copy is unsealed.

#include "test_object.hpp"

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

const Property<float> seal_float = Property<float>::register_property("seal_float", type_a(), Property_metadata{.default_value = 1.0f});
const Property<float> seal_inh   = Property<float>::register_property("seal_inh",   type_a(), Property_metadata{.default_value = 0.0f, .inherits = true});
const Property<float> seal_src   = Property<float>::register_property("seal_src",   type_a(), Property_metadata{.default_value = 2.0f});

} // anonymous namespace

TEST(Sealing, writes_are_rejected_and_nothing_changes)
{
    Test_object o;
    o.set_value(seal_float, 5.0f);
    o.changes.clear();
    o.seal();
    EXPECT_TRUE(o.is_sealed());

    EXPECT_FALSE(o.set_value(seal_float.get(), Property_value{6.0f}));
    EXPECT_FALSE(o.set_current_value(seal_float.get(), Property_value{6.0f}));
    EXPECT_FALSE(o.clear_value(seal_float.get()));
    EXPECT_FALSE(o.set_expression(seal_float.get(), "3 + 4"));
    EXPECT_FALSE(o.apply_local_state(seal_float.get(), std::nullopt));
    EXPECT_FALSE(o.apply_local_state(seal_float.get(), Local_state{Property_value{7.0f}}));
    EXPECT_FALSE(o.apply_local_state(seal_float.get(), Local_state{Expression_text{"8"}}));
    o.set_value(seal_float, 9.0f); // typed overload, same path

    EXPECT_EQ(o.get_value(seal_float), 5.0f);
    EXPECT_EQ(o.get_value_source(seal_float.get()), Value_source::local);
    EXPECT_FALSE(o.get_expression(seal_float.get()).has_value());
    EXPECT_TRUE(o.changes.empty());

    o.unseal();
    EXPECT_FALSE(o.is_sealed());
    EXPECT_TRUE(o.set_value(seal_float.get(), Property_value{6.0f}));
    EXPECT_EQ(o.get_value(seal_float), 6.0f);
    EXPECT_EQ(o.changes.size(), std::size_t{1});
}

TEST(Sealing, reads_inheritance_and_observers_keep_working)
{
    Test_object parent;
    Test_object child;
    child.set_parent(&parent);
    child.seal();

    int observed{0};
    Observer_token token = child.add_observer(seal_inh.get(), [&](Dependency_object&, const Property_changed_args&) { ++observed; });

    parent.set_value(seal_inh, 4.0f);
    EXPECT_EQ(child.get_value(seal_inh), 4.0f);
    EXPECT_EQ(child.get_value_source(seal_inh.get()), Value_source::inherited);
    EXPECT_EQ(child.change_count("seal_inh"), std::size_t{1});
    EXPECT_EQ(observed, 1);

    // The sealed child cannot shadow the inherited value.
    EXPECT_FALSE(child.set_value(seal_inh.get(), Property_value{9.0f}));
    EXPECT_EQ(child.get_value(seal_inh), 4.0f);
}

TEST(Sealing, installed_expression_keeps_evaluating)
{
    Test_object o;
    ASSERT_TRUE(o.set_expression(seal_float.get(), "{seal_src} * 2"));
    EXPECT_EQ(o.get_value(seal_float), 4.0f);
    o.seal();

    EXPECT_TRUE(o.set_value(seal_src.get(), Property_value{3.0f}) == false); // the source is on the sealed object too
    o.unseal();
    EXPECT_TRUE(o.set_value(seal_src.get(), Property_value{3.0f}));
    o.seal();
    EXPECT_EQ(o.get_value(seal_float), 6.0f);
    EXPECT_EQ(o.get_value_source(seal_float.get()), Value_source::expression);
    // The formula itself cannot be replaced or removed while sealed.
    EXPECT_FALSE(o.set_expression(seal_float.get(), "1"));
    EXPECT_FALSE(o.clear_value(seal_float.get()));
    EXPECT_EQ(o.get_value(seal_float), 6.0f);
}

TEST(Sealing, copy_is_unsealed)
{
    Test_object o;
    o.set_value(seal_float, 5.0f);
    o.seal();

    Test_object copy{o};
    EXPECT_FALSE(copy.is_sealed());
    EXPECT_EQ(copy.get_value(seal_float), 5.0f);
    EXPECT_TRUE(copy.set_value(seal_float.get(), Property_value{6.0f}));

    Test_object assigned;
    assigned.seal();
    assigned = o;
    EXPECT_TRUE(assigned.is_sealed()); // assignment copies entries, not the seal state
    EXPECT_FALSE(assigned.set_value(seal_float.get(), Property_value{6.0f}));
}
