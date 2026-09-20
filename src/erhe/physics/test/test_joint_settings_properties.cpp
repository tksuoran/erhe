#include "erhe_physics/physics_joint_settings.hpp"

#include "erhe_property/property_string.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

// The six degrees of freedom of a Physics_joint_settings are eleven
// entry-stored properties each (doc/erhe/property_system.md section 4.22):
// the two arrays the constraint is built from are mirrors of the effective
// values, so a value from any source reaches them.
namespace {

using erhe::physics::Joint_axis_drive;
using erhe::physics::Joint_axis_limit;
using erhe::physics::Physics_joint_settings;

constexpr std::size_t c_trans_x = 0;
constexpr std::size_t c_rot_z   = 5;

} // anonymous namespace

TEST(Joint_settings_properties, every_axis_is_free_by_default)
{
    const std::shared_ptr<Physics_joint_settings> settings = std::make_shared<Physics_joint_settings>("joint");
    for (std::size_t axis = 0; axis < erhe::physics::c_joint_axis_count; ++axis) {
        EXPECT_FALSE(settings->get_axis_limits()[axis].limited);
        EXPECT_FALSE(settings->get_axis_drives()[axis].enabled);
        EXPECT_FALSE(settings->is_axis_fixed(axis));
        EXPECT_EQ(settings->get_value_source(Physics_joint_settings::limit_min_property[axis].get()), erhe::property::Value_source::default_value);
    }
}

TEST(Joint_settings_properties, setters_reach_the_limit_mirror)
{
    const std::shared_ptr<Physics_joint_settings> settings = std::make_shared<Physics_joint_settings>("joint");
    settings->set_axis_limit          (c_rot_z, Joint_axis_limit::limited);
    settings->set_axis_limit_min      (c_rot_z, -0.5f);
    settings->set_axis_limit_max      (c_rot_z,  0.5f);
    settings->set_axis_limit_stiffness(c_rot_z, 120.0f);
    settings->set_axis_limit_damping  (c_rot_z, 3.0f);

    const erhe::physics::Constraint_axis_limit& limit = settings->get_axis_limits()[c_rot_z];
    EXPECT_TRUE(limit.limited);
    EXPECT_FLOAT_EQ(limit.min, -0.5f);
    EXPECT_FLOAT_EQ(limit.max,  0.5f);
    ASSERT_TRUE(limit.stiffness.has_value());
    EXPECT_FLOAT_EQ(limit.stiffness.value(), 120.0f);
    EXPECT_FLOAT_EQ(limit.damping, 3.0f);
    EXPECT_FALSE(settings->is_axis_fixed(c_rot_z));

    settings->set_axis_limit_min(c_rot_z, 0.0f);
    settings->set_axis_limit_max(c_rot_z, 0.0f);
    EXPECT_TRUE(settings->is_axis_fixed(c_rot_z));
}

// An unwritten side of a limit is the unbounded one: the backend's own
// unbounded value, which differs between a translation and a rotation axis.
TEST(Joint_settings_properties, an_unwritten_limit_side_is_unbounded)
{
    const std::shared_ptr<Physics_joint_settings> settings = std::make_shared<Physics_joint_settings>("joint");
    settings->set_axis_limit(c_trans_x, Joint_axis_limit::limited);
    settings->set_axis_limit(c_rot_z,   Joint_axis_limit::limited);
    settings->set_axis_limit_min(c_trans_x, -1.0f);

    EXPECT_FLOAT_EQ(settings->get_axis_limits()[c_trans_x].min, -1.0f);
    EXPECT_FLOAT_EQ(settings->get_axis_limits()[c_trans_x].max, std::numeric_limits<float>::max());
    EXPECT_NEAR(settings->get_axis_limits()[c_rot_z].min, -3.14159265f, 1.0e-5f);
    EXPECT_NEAR(settings->get_axis_limits()[c_rot_z].max,  3.14159265f, 1.0e-5f);
}

TEST(Joint_settings_properties, a_drive_mirrors_its_mode_and_its_unlimited_force)
{
    const std::shared_ptr<Physics_joint_settings> settings = std::make_shared<Physics_joint_settings>("joint");
    settings->set_axis_drive(c_rot_z, Joint_axis_drive::force);
    settings->set_axis_drive_stiffness(c_rot_z, 80.0f);
    settings->set_axis_drive_position_target(c_rot_z, 0.25f);

    const erhe::physics::Constraint_axis_drive& drive = settings->get_axis_drives()[c_rot_z];
    EXPECT_TRUE(drive.enabled);
    EXPECT_TRUE(drive.use_position_target);
    EXPECT_FLOAT_EQ(drive.position_target, 0.25f);
    EXPECT_FLOAT_EQ(drive.stiffness, 80.0f);
    EXPECT_TRUE(std::isinf(drive.max_force)); // zero is the unlimited force

    settings->set_axis_drive_max_force(c_rot_z, 500.0f);
    EXPECT_FLOAT_EQ(settings->get_axis_drives()[c_rot_z].max_force, 500.0f);

    settings->set_axis_drive(c_rot_z, Joint_axis_drive::off);
    EXPECT_FALSE(settings->get_axis_drives()[c_rot_z].enabled);
}

// The untyped path every generic writer takes (MCP set_item_property, a
// property serializer, Property_set_operation), with the enumeration labels.
TEST(Joint_settings_properties, untyped_write_with_enum_labels_reaches_the_mirror)
{
    const std::shared_ptr<Physics_joint_settings> settings = std::make_shared<Physics_joint_settings>("joint");
    const erhe::property::Dependency_property* const property =
        erhe::property::Property_registry::get().find_for_object(*settings, "rot_z_limit");
    ASSERT_NE(property, nullptr);
    EXPECT_EQ(property->get_type(), erhe::property::Property_type::enumeration);

    const std::optional<erhe::property::Property_value> parsed = erhe::property::parse_value(*property, "Limited");
    ASSERT_TRUE(parsed.has_value());
    settings->set_value(*property, parsed.value());
    EXPECT_TRUE(settings->get_axis_limits()[c_rot_z].limited);
    EXPECT_EQ(erhe::property::to_string(*property, settings->get_value(*property)), "Limited");
}

// Every axis property inherits, so a value supplied by a folder or a style is
// an ordinary opinion the item's own local value overrides.
TEST(Joint_settings_properties, an_inherited_value_reaches_the_mirror)
{
    const std::shared_ptr<Physics_joint_settings> holder   = std::make_shared<Physics_joint_settings>("holder");
    const std::shared_ptr<Physics_joint_settings> settings = std::make_shared<Physics_joint_settings>("joint");
    settings->set_parent(holder);

    holder->set_axis_limit    (c_rot_z, Joint_axis_limit::limited);
    holder->set_axis_limit_max(c_rot_z, 0.75f);
    EXPECT_EQ(settings->get_value_source(Physics_joint_settings::limit_max_property[c_rot_z].get()), erhe::property::Value_source::inherited);
    EXPECT_TRUE(settings->get_axis_limits()[c_rot_z].limited);
    EXPECT_FLOAT_EQ(settings->get_axis_limits()[c_rot_z].max, 0.75f);

    settings->set_axis_limit_max(c_rot_z, 0.25f);
    EXPECT_FLOAT_EQ(settings->get_axis_limits()[c_rot_z].max, 0.25f);
}

TEST(Joint_settings_properties, clone_carries_the_axes)
{
    const std::shared_ptr<Physics_joint_settings> settings = std::make_shared<Physics_joint_settings>("joint");
    settings->set_axis_limit    (c_trans_x, Joint_axis_limit::limited);
    settings->set_axis_limit_min(c_trans_x, 0.0f);
    settings->set_axis_limit_max(c_trans_x, 0.0f);
    settings->set_axis_drive    (c_rot_z, Joint_axis_drive::acceleration);

    const Physics_joint_settings clone{*settings.get()};
    EXPECT_TRUE(clone.is_axis_fixed(c_trans_x));
    EXPECT_TRUE(clone.get_axis_drives()[c_rot_z].enabled);
    EXPECT_EQ(clone.get_value(Physics_joint_settings::drive_property[c_rot_z]), Joint_axis_drive::acceleration);
}
