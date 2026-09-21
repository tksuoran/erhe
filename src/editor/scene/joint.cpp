#include "scene/joint.hpp"
#include "scene/joint_system.hpp"
#include "scene/scene_root.hpp"

#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/node.hpp"

namespace editor {

namespace {

using erhe::property::Object_reference;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Weak_object_reference;

using Node_traits     = erhe::property::Member_value_traits<std::weak_ptr<erhe::scene::Node>>;
using Settings_traits = erhe::property::Member_value_traits<std::shared_ptr<erhe::physics::Physics_joint_settings>>;

constexpr std::string_view c_group = "Joint";

} // anonymous namespace

const Property<Weak_object_reference> Joint::body_0_property = Property<Weak_object_reference>::register_property(
    "body_0", Joint::property_owner_type(),
    Property_metadata{
        .inherits = false,
        .ui       = Property_ui{
            .group                = c_group,
            .tooltip              = "First frame node: the joint's frame in the space of the nearest self-or-ancestor rigid body of this node",
            .label                = "Body 0",
            .reference_item_types = erhe::Item_type::xformable
        }
    },
    Node_traits::validate
);

const Property<Weak_object_reference> Joint::body_1_property = Property<Weak_object_reference>::register_property(
    "body_1", Joint::property_owner_type(),
    Property_metadata{
        .inherits = false,
        .ui       = Property_ui{
            .group                = c_group,
            .tooltip              = "Second frame node; none, or a node with no rigid body on its chain, anchors the joint to the world",
            .label                = "Body 1",
            .reference_item_types = erhe::Item_type::xformable
        }
    },
    Node_traits::validate
);

const Property<Object_reference> Joint::joint_settings_property = Property<Object_reference>::register_property(
    "joint_settings", Joint::property_owner_type(),
    Property_metadata{
        .inherits = true,
        .ui       = Property_ui{
            .group                = c_group,
            .tooltip              = "Shared six-dof limits and drives the constraint is built from",
            .label                = "Joint Settings",
            .reference_item_types = erhe::Item_type::physics_joint_settings
        }
    },
    Settings_traits::validate
);

const Property<bool> Joint::enable_collision_property = Property<bool>::register_property(
    "enable_collision", Joint::property_owner_type(),
    Property_metadata{
        .default_value = false,
        .inherits      = true,
        .ui            = Property_ui{
            .group   = c_group,
            .tooltip = "Let the two joined bodies collide with each other",
            .label   = "Enable Collision"
        }
    }
);

Joint::Joint()           = default;
Joint::~Joint() noexcept = default;

Joint::Joint(const Joint& src) = default;

Joint& Joint::operator=(const Joint& src) = default;

Joint::Joint(const std::string_view name)
    : Item{name}
{
}

Joint::Joint(
    const std::string_view                                        name,
    const std::shared_ptr<erhe::scene::Node>&                     body_0,
    const std::shared_ptr<erhe::scene::Node>&                     body_1,
    const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings,
    const bool                                                    enable_collision
)
    : Item{name}
{
    // Local values only where the argument differs from the property default,
    // so a joint that states nothing stays open to a holder above it.
    if (body_0)           { set_value(body_0_property, Node_traits::to_value(body_0)); }
    if (body_1)           { set_value(body_1_property, Node_traits::to_value(body_1)); }
    if (settings)         { set_value(joint_settings_property, Settings_traits::to_value(settings)); }
    if (enable_collision) { set_value(enable_collision_property, true); }
}

Joint::Joint(const Joint& src, erhe::for_clone)
    : Item{src, erhe::for_clone{}}
{
}

void Joint::handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits)
{
    Imageable::handle_flag_bits_update(old_flag_bits, new_flag_bits);
    if (((old_flag_bits ^ new_flag_bits) & erhe::Item_flags::active) == 0) {
        return;
    }
    Joint_system* const system = find_joint_system(*this);
    if (system != nullptr) {
        system->on_joint_active_changed(*this);
    }
}

void Joint::on_property_changed(const erhe::property::Property_changed_args& args)
{
    if (
        (&args.property != body_0_property.get_ptr())         &&
        (&args.property != body_1_property.get_ptr())         &&
        (&args.property != joint_settings_property.get_ptr()) &&
        (&args.property != enable_collision_property.get_ptr())
    ) {
        return;
    }
    Joint_system* const system = find_joint_system(*this);
    if (system != nullptr) {
        system->on_values_changed(*this);
    }
}

auto Joint::get_body_0() const -> std::shared_ptr<erhe::scene::Node>
{
    return Node_traits::from_value(get_value(body_0_property)).lock();
}

void Joint::set_body_0(const std::shared_ptr<erhe::scene::Node>& node)
{
    set_value(body_0_property, Node_traits::to_value(node));
}

auto Joint::get_body_1() const -> std::shared_ptr<erhe::scene::Node>
{
    return Node_traits::from_value(get_value(body_1_property)).lock();
}

void Joint::set_body_1(const std::shared_ptr<erhe::scene::Node>& node)
{
    set_value(body_1_property, Node_traits::to_value(node));
}

auto Joint::get_settings() const -> std::shared_ptr<erhe::physics::Physics_joint_settings>
{
    return Settings_traits::from_value(get_value(joint_settings_property));
}

void Joint::set_settings(const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings)
{
    set_value(joint_settings_property, Settings_traits::to_value(settings));
}

auto Joint::get_enable_collision() const -> bool
{
    return get_value(enable_collision_property);
}

void Joint::set_enable_collision(const bool enable_collision)
{
    set_value(enable_collision_property, enable_collision);
}

auto Joint::get_constraint() const -> erhe::physics::IConstraint*
{
    const Joint_system* const system = find_joint_system(*this);
    return (system != nullptr) ? system->get_constraint(*this) : nullptr;
}

auto Joint::get_constraint_state() const -> const Joint_constraint_state*
{
    const Joint_system* const system = find_joint_system(*this);
    return (system != nullptr) ? system->get_constraint_state(*this) : nullptr;
}

void Joint::rebuild()
{
    Joint_system* const system = find_joint_system(*this);
    if (system != nullptr) {
        system->rebuild(*this);
    }
}

auto find_joint_system(const Joint& joint) -> Joint_system*
{
    Scene_root* const scene_root = dynamic_cast<Scene_root*>(joint.get_item_host());
    return (scene_root != nullptr) ? &scene_root->get_joint_system() : nullptr;
}

} // namespace editor
