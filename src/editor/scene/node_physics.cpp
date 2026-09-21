#include "scene/node_physics.hpp"

#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_property/attached_group.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_system.hpp"

namespace editor {

namespace {

using erhe::physics::Motion_mode;
using erhe::property::Dependency_object;
using erhe::property::Dependency_property;
using erhe::property::Object_reference;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;
using erhe::property::Value_source;
using erhe::property::Weak_object_reference;

using Material_traits = erhe::property::Member_value_traits<std::shared_ptr<erhe::physics::Physics_material>>;
using Filter_traits   = erhe::property::Member_value_traits<std::shared_ptr<erhe::physics::Collision_filter>>;
using Mesh_traits     = erhe::property::Member_value_traits<std::weak_ptr<erhe::scene::Mesh>>;

constexpr std::string_view c_group = "Rigid Body";

// A body that cannot move has no use for gravity, velocities or a mass.
[[nodiscard]] auto is_movable(const Dependency_object& object) -> bool
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&object);
    if (node == nullptr) {
        return false;
    }
    const Motion_mode motion_mode = node->get_value(Node_physics::motion_mode_property);
    return (motion_mode != Motion_mode::e_none) && (motion_mode != Motion_mode::e_static);
}

} // anonymous namespace

auto Node_physics::property_owner_type() -> erhe::property::Owner_type
{
    // Node_physics is not a Dependency_object, so there is no Item<> to
    // allocate the id: the registering class's own id sits directly under the
    // root and serves only to qualify the names (Node_physics.motion_mode).
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
        erhe::property::root_owner_type, "Node_physics"
    );
    return s_id;
}

// The key property comes first, so its registration is complete when the rest
// of the group takes attached_group_visible_when on it (D1).
const Property<Motion_mode> Node_physics::motion_mode_property = Property<Motion_mode>::register_attached(
    "motion_mode", Node_physics::property_owner_type(), erhe::scene::Node::property_owner_type(), erhe::physics::c_motion_mode_enum_info,
    Property_metadata{
        .default_value    = erhe::property::make_value(Motion_mode::e_none),
        .property_changed = erhe::scene::node_system_property_changed,
        .inherits         = false,
        .ui               = Property_ui{
            .group   = c_group,
            .tooltip = "The intended mode; 'None' means the prim simulates nothing. A static trigger body is created kinematic non-physical",
            .label   = "Motion Mode"
        }
    }
);

namespace {

// Every non-key value of the group is listed exactly on the prims carrying it
// (D1), and a change of any of them reaches the scene's physics system.
[[nodiscard]] auto group_ui(
    const std::string_view                  label,
    const std::string_view                  tooltip,
    const erhe::property::Property_ui::Visible_when holder_predicate = {}
) -> Property_ui
{
    return Property_ui{
        .group        = c_group,
        .tooltip      = tooltip,
        .label        = label,
        .visible_when = (holder_predicate != nullptr)
            ? erhe::property::attached_group_visible_when(Node_physics::motion_mode_property.get(), holder_predicate)
            : erhe::property::attached_group_visible_when(Node_physics::motion_mode_property.get())
    };
}

} // anonymous namespace

const Property<bool> Node_physics::is_trigger_property = Property<bool>::register_attached(
    "is_trigger", Node_physics::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = false,
        .property_changed = erhe::scene::node_system_property_changed,
        .inherits         = true,
        .ui               = group_ui("Is Trigger", "Sensor body: reports overlaps, no collision response (recreates the rigid body)")
    }
);
const Property<float> Node_physics::mass_property = Property<float>::register_attached(
    "mass", Node_physics::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = 1.0f,
        .property_changed = erhe::scene::node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{
            .min          = 0.01f,
            .max          = 1000.0f,
            .presentation = Property_ui::Presentation::slider,
            .logarithmic  = true,
            .group        = c_group,
            .tooltip      = "kg; while no value is set (source default) the body's mass is its shape mass scaled by the material density",
            .label        = "Mass",
            .visible_when = erhe::property::attached_group_visible_when(Node_physics::motion_mode_property.get(), is_movable)
        }
    },
    [](const Property_value& value) -> bool { return std::get<float>(value) > 0.0f; }
);
const Property<float> Node_physics::gravity_factor_property = Property<float>::register_attached(
    "gravity_factor", Node_physics::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = 1.0f,
        .property_changed = erhe::scene::node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{
            .min          = 0.0f,
            .max          = 2.0f,
            .presentation = Property_ui::Presentation::slider,
            .group        = c_group,
            .label        = "Gravity Factor",
            .visible_when = erhe::property::attached_group_visible_when(Node_physics::motion_mode_property.get(), is_movable)
        }
    }
);
const Property<glm::vec3> Node_physics::initial_linear_velocity_property = Property<glm::vec3>::register_attached(
    "initial_linear_velocity", Node_physics::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = glm::vec3{0.0f},
        .property_changed = erhe::scene::node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{
            .step         = 0.01f,
            .group        = c_group,
            .tooltip      = "World space; applied when the rigid body is (re)created",
            .label        = "Initial Linear Velocity",
            .visible_when = erhe::property::attached_group_visible_when(Node_physics::motion_mode_property.get(), is_movable)
        }
    }
);
const Property<glm::vec3> Node_physics::initial_angular_velocity_property = Property<glm::vec3>::register_attached(
    "initial_angular_velocity", Node_physics::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = glm::vec3{0.0f},
        .property_changed = erhe::scene::node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{
            .step         = 0.01f,
            .group        = c_group,
            .tooltip      = "World space; applied when the rigid body is (re)created",
            .label        = "Initial Angular Velocity",
            .visible_when = erhe::property::attached_group_visible_when(Node_physics::motion_mode_property.get(), is_movable)
        }
    }
);
const Property<glm::vec3> Node_physics::center_of_mass_offset_property = Property<glm::vec3>::register_attached(
    "center_of_mass_offset", Node_physics::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = glm::vec3{0.0f},
        .property_changed = erhe::scene::node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{
            .step         = 0.01f,
            .group        = c_group,
            .tooltip      = "Offset-center-of-mass wrapper around the collision shape (recreates the rigid body)",
            .label        = "Center of Mass",
            .visible_when = erhe::property::attached_group_visible_when(Node_physics::motion_mode_property.get())
        }
    }
);
const Property<Object_reference> Node_physics::physics_material_property = Property<Object_reference>::register_attached(
    "physics_material", Node_physics::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .property_changed = erhe::scene::node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{
            .group                = c_group,
            .tooltip              = "Shared material carrying friction, restitution, damping, wind receptivity and density; none behaves like the material defaults",
            .label                = "Physics Material",
            .visible_when         = erhe::property::attached_group_visible_when(Node_physics::motion_mode_property.get()),
            .reference_item_types = erhe::Item_type::physics_material
        }
    },
    Material_traits::validate
);
const Property<Object_reference> Node_physics::collision_filter_property = Property<Object_reference>::register_attached(
    "collision_filter", Node_physics::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .property_changed = erhe::scene::node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{
            .group                = c_group,
            .label                = "Collision Filter",
            .visible_when         = erhe::property::attached_group_visible_when(Node_physics::motion_mode_property.get()),
            .reference_item_types = erhe::Item_type::collision_filter
        }
    },
    Filter_traits::validate
);
// Per instance, and weak: the mesh is a prim of this body's own subtree, so
// the value must not be an ownership edge and must not reach the bodies below.
const Property<Weak_object_reference> Node_physics::collision_mesh_property = Property<Weak_object_reference>::register_attached(
    "collision_mesh", Node_physics::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .inherits = false,
        .ui       = Property_ui{
            .group                = c_group,
            .tooltip              = "The mesh the convex hull / triangle shape was built from; none means the body's own mesh. Recorded for the exporters - changing it does not rebuild the shape",
            .label                = "Collision Mesh",
            .visible_when         = erhe::property::attached_group_visible_when(Node_physics::motion_mode_property.get()),
            .reference_item_types = erhe::Item_type::mesh
        }
    },
    Mesh_traits::validate
);

auto Node_physics::all_properties() -> const std::vector<const Dependency_property*>&
{
    static const std::vector<const Dependency_property*> s_properties{
        motion_mode_property             .get_ptr(),
        is_trigger_property              .get_ptr(),
        mass_property                    .get_ptr(),
        gravity_factor_property          .get_ptr(),
        initial_linear_velocity_property .get_ptr(),
        initial_angular_velocity_property.get_ptr(),
        center_of_mass_offset_property   .get_ptr(),
        physics_material_property        .get_ptr(),
        collision_filter_property        .get_ptr(),
        collision_mesh_property          .get_ptr()
    };
    return s_properties;
}

auto motion_state_of(const erhe::physics::Motion_mode mode) -> Motion_state
{
    switch (mode) {
        case erhe::physics::Motion_mode::e_none:                   return Motion_state::e_none;
        case erhe::physics::Motion_mode::e_static:                 return Motion_state::e_static;
        case erhe::physics::Motion_mode::e_kinematic_non_physical: return Motion_state::e_kinematic;
        case erhe::physics::Motion_mode::e_kinematic_physical:     return Motion_state::e_kinematic;
        case erhe::physics::Motion_mode::e_dynamic:                return Motion_state::e_dynamic;
        default:                                                   return Motion_state::e_none;
    }
}

auto carries_node_physics(const erhe::scene::Node& node) -> bool
{
    return erhe::property::carries_attached_group(node, Node_physics::motion_mode_property.get());
}

auto read_node_physics(const erhe::scene::Node& node) -> std::optional<Node_physics_data>
{
    if (!carries_node_physics(node)) {
        return {};
    }
    Node_physics_data data{};
    data.motion_mode              = node.get_value(Node_physics::motion_mode_property);
    data.is_trigger               = node.get_value(Node_physics::is_trigger_property);
    if (node.get_value_source(Node_physics::mass_property) != Value_source::default_value) {
        data.mass = node.get_value(Node_physics::mass_property);
    }
    data.gravity_factor           = node.get_value(Node_physics::gravity_factor_property);
    data.initial_linear_velocity  = node.get_value(Node_physics::initial_linear_velocity_property);
    data.initial_angular_velocity = node.get_value(Node_physics::initial_angular_velocity_property);
    data.center_of_mass_offset    = node.get_value(Node_physics::center_of_mass_offset_property);
    data.physics_material         = Material_traits::from_value(node.get_value(Node_physics::physics_material_property));
    data.collision_filter         = Filter_traits::from_value(node.get_value(Node_physics::collision_filter_property));
    const std::weak_ptr<erhe::scene::Mesh> collision_mesh = Mesh_traits::from_value(node.get_value(Node_physics::collision_mesh_property));
    data.collision_mesh           = collision_mesh.lock();
    // A value that names a control block whose object is gone: the mesh an
    // undo took out of the scene while the body stayed.
    const std::weak_ptr<erhe::scene::Mesh> unset{};
    const bool names_a_mesh =
        collision_mesh.owner_before(unset) ||
        unset.owner_before(collision_mesh);
    data.lost_collision_mesh      = names_a_mesh && collision_mesh.expired();
    return data;
}

auto make_node_physics_create_info(const erhe::scene::Node& node) -> erhe::physics::IRigid_body_create_info
{
    erhe::physics::IRigid_body_create_info create_info{};
    const std::optional<Node_physics_data> data = read_node_physics(node);
    if (!data.has_value()) {
        return create_info;
    }
    create_info.mass             = data.value().mass;
    create_info.debug_label      = node.get_name();
    create_info.motion_mode      = data.value().motion_mode;
    create_info.linear_velocity  = data.value().initial_linear_velocity;
    create_info.angular_velocity = data.value().initial_angular_velocity;
    create_info.gravity_factor   = data.value().gravity_factor;
    create_info.is_sensor        = data.value().is_trigger;
    create_info.physics_material = data.value().physics_material;
    create_info.collision_filter = data.value().collision_filter;
    return create_info;
}

void write_node_physics_create_info(erhe::scene::Node& node, const erhe::physics::IRigid_body_create_info& create_info)
{
    // A field at its default stays unset, so a node above or a style can hold
    // it; the key property is always written, because the create info says the
    // node is to carry a body.
    if (create_info.is_sensor)                           { node.set_value(Node_physics::is_trigger_property, true); }
    if (create_info.mass.has_value())                    { node.set_value(Node_physics::mass_property, create_info.mass.value()); }
    if (create_info.gravity_factor != 1.0f)              { node.set_value(Node_physics::gravity_factor_property, create_info.gravity_factor); }
    if (create_info.linear_velocity != glm::vec3{0.0f})  { node.set_value(Node_physics::initial_linear_velocity_property, create_info.linear_velocity); }
    if (create_info.angular_velocity != glm::vec3{0.0f}) { node.set_value(Node_physics::initial_angular_velocity_property, create_info.angular_velocity); }
    if (create_info.collision_shape) {
        const glm::vec3 offset = create_info.collision_shape->get_offset().value_or(glm::vec3{0.0f});
        if (offset != glm::vec3{0.0f})                   { node.set_value(Node_physics::center_of_mass_offset_property, offset); }
    }
    if (create_info.physics_material)                    { node.set_value(Node_physics::physics_material_property, Material_traits::to_value(create_info.physics_material)); }
    if (create_info.collision_filter)                    { node.set_value(Node_physics::collision_filter_property, Filter_traits::to_value(create_info.collision_filter)); }
    node.set_value(Node_physics::motion_mode_property, create_info.motion_mode);
}

void clear_node_physics(erhe::scene::Node& node)
{
    node.clear_value(Node_physics::motion_mode_property);
}

} // namespace editor
