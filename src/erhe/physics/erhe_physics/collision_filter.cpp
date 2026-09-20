#include "erhe_physics/collision_filter.hpp"

namespace erhe::physics {

namespace {

using erhe::property::Property;
using erhe::property::Property_flags;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

const erhe::property::Owner_type c_owner = Collision_filter::property_owner_type();

// The KHR_physics_rigid_bodies collisionFilters entry carries the three
// lists itself (Property_flags::native_gltf, D32), so the glTF property
// serializer writes no value entry of its own for them; the USD
// PhysicsCollisionGroup prim spells them under the same erhe: attribute
// names a local value would take (doc/erhe/property_system.md section
// 4.21).
constexpr uint32_t c_native = Property_flags::serialize | Property_flags::native_gltf;

[[nodiscard]] auto list_ui(const std::string_view label, const std::string_view tooltip) -> Property_ui
{
    return Property_ui{
        .group      = "Collision Filter",
        .tooltip    = tooltip,
        .array_size = Property_ui::Array_size::editable,
        .label      = label
    };
}

} // anonymous namespace

// A filter states the systems of its own body, so none of the three lists
// inherits: an empty list is what the semantics above are written against.
const Property<std::vector<std::string>> Collision_filter::collision_systems_property = Property<std::vector<std::string>>::register_property(
    "collision_systems", c_owner,
    Property_metadata{.flags = c_native, .ui = list_ui("Collision Systems", "Systems this filter's body belongs to")}
);
const Property<std::vector<std::string>> Collision_filter::collide_with_systems_property = Property<std::vector<std::string>>::register_property(
    "collide_with_systems", c_owner,
    Property_metadata{.flags = c_native, .ui = list_ui("Collide With", "Non-empty = collide only with these systems (allowlist)")}
);
const Property<std::vector<std::string>> Collision_filter::not_collide_with_systems_property = Property<std::vector<std::string>>::register_property(
    "not_collide_with_systems", c_owner,
    Property_metadata{.flags = c_native, .ui = list_ui("Not Collide With", "Used when Collide With is empty: never collide with these")}
);

Collision_filter::Collision_filter()                                   = default;
Collision_filter::Collision_filter(const Collision_filter&)            = default;
Collision_filter& Collision_filter::operator=(const Collision_filter&) = default;
Collision_filter::~Collision_filter() noexcept                         = default;

Collision_filter::Collision_filter(const std::string_view name)
    : Item{name}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

void Collision_filter::on_property_changed(const erhe::property::Property_changed_args& args)
{
    if (!erhe::property::is_owner_type_or_descendant(c_owner, args.property.get_owner_type())) {
        return;
    }
    const erhe::property::Dependency_property* const changed = &args.property;
    if (changed == collision_systems_property.get_ptr()) {
        m_collision_systems = get_value(collision_systems_property);
    } else if (changed == collide_with_systems_property.get_ptr()) {
        m_collide_with_systems = get_value(collide_with_systems_property);
    } else if (changed == not_collide_with_systems_property.get_ptr()) {
        m_not_collide_with_systems = get_value(not_collide_with_systems_property);
    }
}

} // namespace erhe::physics
