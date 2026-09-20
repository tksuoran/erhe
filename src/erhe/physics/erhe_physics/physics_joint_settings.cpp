#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_physics/joint_reach.hpp"
#include "erhe_physics/physics_log.hpp"

#include <glm/gtc/constants.hpp>

#include <limits>
#include <string>

namespace erhe::physics {

namespace {

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

const erhe::property::Owner_type c_owner = Physics_joint_settings::property_owner_type();

// Both file formats carry the six axes in their own joint entry - the
// KHR_physics_rigid_bodies `physicsJoints` limits and drives, the USD
// PhysicsLimitAPI and PhysicsDriveAPI instances - so the property
// serializers write no value entry of their own for them (D32).
constexpr uint32_t c_native = erhe::property::Property_flags::serialize | erhe::property::Property_flags::native_gltf;

constexpr std::string_view c_axis_tokens[c_joint_axis_count] = {
    std::string_view{"trans_x"},
    std::string_view{"trans_y"},
    std::string_view{"trans_z"},
    std::string_view{"rot_x"},
    std::string_view{"rot_y"},
    std::string_view{"rot_z"}
};

constexpr std::string_view c_axis_groups[c_joint_axis_count] = {
    std::string_view{"Translation X"},
    std::string_view{"Translation Y"},
    std::string_view{"Translation Z"},
    std::string_view{"Rotation X"},
    std::string_view{"Rotation Y"},
    std::string_view{"Rotation Z"}
};

constexpr erhe::property::Enum_entry c_joint_axis_limit_entries[] = {
    {"Free",    static_cast<int32_t>(Joint_axis_limit::free)},
    {"Limited", static_cast<int32_t>(Joint_axis_limit::limited)}
};

constexpr erhe::property::Enum_entry c_joint_axis_drive_entries[] = {
    {"Off",          static_cast<int32_t>(Joint_axis_drive::off)},
    {"Force",        static_cast<int32_t>(Joint_axis_drive::force)},
    {"Acceleration", static_cast<int32_t>(Joint_axis_drive::acceleration)}
};

// Which of the axis's own enumeration rows decides whether a value row is
// shown, what the value means, and whether it is bounded below.
enum class Axis_row_visibility { when_limited, when_driven };
enum class Axis_row_units      { plain, angle_on_rotation_axis };
enum class Axis_row_range      { any, non_negative };

[[nodiscard]] auto make_visible_when(const std::size_t axis, const Axis_row_visibility visibility) -> Property_ui::Visible_when
{
    if (visibility == Axis_row_visibility::when_limited) {
        return [axis](const erhe::property::Dependency_object& object) -> bool {
            return object.get_value(Physics_joint_settings::limit_property[axis]) == Joint_axis_limit::limited;
        };
    }
    return [axis](const erhe::property::Dependency_object& object) -> bool {
        return object.get_value(Physics_joint_settings::drive_property[axis]) != Joint_axis_drive::off;
    };
}

// One row of the per-axis table: the same registration for each of the six
// axes, whose token is the prefix of the property name and whose group is
// the Properties window section the row lands in.
[[nodiscard]] auto register_float_row(
    const std::string_view    suffix,
    const std::string_view    label,
    const std::string_view    tooltip,
    const Axis_row_visibility visibility,
    const Axis_row_units      units,
    const Axis_row_range      range
) -> std::array<Property<float>, c_joint_axis_count>
{
    std::array<Property<float>, c_joint_axis_count> row{};
    for (std::size_t axis = 0; axis < c_joint_axis_count; ++axis) {
        const std::string name = std::string{c_axis_tokens[axis]} + std::string{suffix};
        Property_ui ui{
            .min          = (range == Axis_row_range::non_negative) ? std::optional<float>{0.0f} : std::optional<float>{},
            .step         = 0.01f,
            .presentation = ((units == Axis_row_units::angle_on_rotation_axis) && is_joint_rotation_axis(axis))
                ? Property_ui::Presentation::angle_degrees
                : Property_ui::Presentation::plain,
            .group        = c_axis_groups[axis],
            .tooltip      = tooltip,
            .label        = label,
            .visible_when = make_visible_when(axis, visibility)
        };
        row[axis] = Property<float>::register_property(
            name, c_owner,
            Property_metadata{.default_value = 0.0f, .inherits = true, .flags = c_native, .ui = std::move(ui)}
        );
    }
    return row;
}

template <typename T>
[[nodiscard]] auto register_enum_row(
    const std::string_view                suffix,
    const std::string_view                label,
    const std::string_view                tooltip,
    const erhe::property::Enum_info&      enum_info,
    const T                               default_value
) -> std::array<Property<T>, c_joint_axis_count>
{
    std::array<Property<T>, c_joint_axis_count> row{};
    for (std::size_t axis = 0; axis < c_joint_axis_count; ++axis) {
        const std::string name = std::string{c_axis_tokens[axis]} + std::string{suffix};
        row[axis] = Property<T>::register_property(
            name, c_owner, enum_info,
            Property_metadata{
                .default_value = erhe::property::make_value(default_value),
                .inherits      = true,
                .flags         = c_native,
                .ui            = Property_ui{.group = c_axis_groups[axis], .tooltip = tooltip, .label = label}
            }
        );
    }
    return row;
}

} // anonymous namespace

const erhe::property::Enum_info c_joint_axis_limit_enum_info{"Joint_axis_limit", c_joint_axis_limit_entries};
const erhe::property::Enum_info c_joint_axis_drive_enum_info{"Joint_axis_drive", c_joint_axis_drive_entries};

auto joint_axis_token(const std::size_t axis) -> std::string_view
{
    return (axis < c_joint_axis_count) ? c_axis_tokens[axis] : std::string_view{};
}

auto is_joint_rotation_axis(const std::size_t axis) -> bool
{
    return axis >= 3;
}

// The two enumeration rows are registered first: the value rows' visible_when
// callbacks read them (at draw time, so the order only keeps the reading
// obvious).
const std::array<Property<Joint_axis_limit>, c_joint_axis_count> Physics_joint_settings::limit_property = register_enum_row<Joint_axis_limit>(
    "_limit", "Limit", "Free leaves the axis alone; Limited confines it to Min .. Max (equal values lock it)",
    c_joint_axis_limit_enum_info, Joint_axis_limit::free
);
const std::array<Property<Joint_axis_drive>, c_joint_axis_count> Physics_joint_settings::drive_property = register_enum_row<Joint_axis_drive>(
    "_drive", "Drive", "The axis motor; Acceleration is approximated as Force",
    c_joint_axis_drive_enum_info, Joint_axis_drive::off
);

const std::array<Property<float>, c_joint_axis_count> Physics_joint_settings::limit_min_property = register_float_row(
    "_limit_min", "Min", "Lower end of the limit; unset leaves that side unbounded",
    Axis_row_visibility::when_limited, Axis_row_units::angle_on_rotation_axis, Axis_row_range::any
);
const std::array<Property<float>, c_joint_axis_count> Physics_joint_settings::limit_max_property = register_float_row(
    "_limit_max", "Max", "Upper end of the limit; unset leaves that side unbounded",
    Axis_row_visibility::when_limited, Axis_row_units::angle_on_rotation_axis, Axis_row_range::any
);
const std::array<Property<float>, c_joint_axis_count> Physics_joint_settings::limit_stiffness_property = register_float_row(
    "_limit_stiffness", "Limit Stiffness", "Zero is a hard limit; a non-zero value is the soft-limit spring",
    Axis_row_visibility::when_limited, Axis_row_units::plain, Axis_row_range::non_negative
);
const std::array<Property<float>, c_joint_axis_count> Physics_joint_settings::limit_damping_property = register_float_row(
    "_limit_damping", "Limit Damping", "Damping of the soft-limit spring",
    Axis_row_visibility::when_limited, Axis_row_units::plain, Axis_row_range::non_negative
);

const std::array<Property<float>, c_joint_axis_count> Physics_joint_settings::drive_max_force_property = register_float_row(
    "_drive_max_force", "Max Force", "Largest force (or torque) the drive applies; zero is unlimited",
    Axis_row_visibility::when_driven, Axis_row_units::plain, Axis_row_range::non_negative
);
const std::array<Property<float>, c_joint_axis_count> Physics_joint_settings::drive_position_target_property = register_float_row(
    "_drive_position_target", "Position Target", "Target the position motor pulls the axis to",
    Axis_row_visibility::when_driven, Axis_row_units::angle_on_rotation_axis, Axis_row_range::any
);
const std::array<Property<float>, c_joint_axis_count> Physics_joint_settings::drive_velocity_target_property = register_float_row(
    "_drive_velocity_target", "Velocity Target", "Target the velocity motor drives the axis at",
    Axis_row_visibility::when_driven, Axis_row_units::angle_on_rotation_axis, Axis_row_range::any
);
const std::array<Property<float>, c_joint_axis_count> Physics_joint_settings::drive_stiffness_property = register_float_row(
    "_drive_stiffness", "Drive Stiffness", "Greater than zero selects a position motor, zero a velocity motor",
    Axis_row_visibility::when_driven, Axis_row_units::plain, Axis_row_range::non_negative
);
const std::array<Property<float>, c_joint_axis_count> Physics_joint_settings::drive_damping_property = register_float_row(
    "_drive_damping", "Drive Damping", "Damping of the drive spring",
    Axis_row_visibility::when_driven, Axis_row_units::plain, Axis_row_range::non_negative
);

Physics_joint_settings::Physics_joint_settings(const Physics_joint_settings&)            = default;
Physics_joint_settings& Physics_joint_settings::operator=(const Physics_joint_settings&) = default;
Physics_joint_settings::~Physics_joint_settings() noexcept                               = default;

Physics_joint_settings::Physics_joint_settings()
{
    refresh_mirrors();
}

Physics_joint_settings::Physics_joint_settings(const std::string_view name)
    : Item{name}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
    refresh_mirrors();
}

void Physics_joint_settings::on_property_changed(const erhe::property::Property_changed_args& args)
{
    // Only the six axes shape the constraint. The properties inherited from
    // Item_base (visible, active, name, ...) belong to an ancestor owner type
    // and are excluded by this test.
    if (!erhe::property::is_owner_type_or_descendant(c_owner, args.property.get_owner_type())) {
        return;
    }
    refresh_mirrors();
}

void Physics_joint_settings::refresh_mirrors()
{
    bool acceleration_drive = false;
    for (std::size_t axis = 0; axis < c_joint_axis_count; ++axis) {
        // An unwritten side of the limit is the unbounded one: Jolt treats a
        // translation limit of lowest() .. max() as a free axis and clamps a
        // rotation limit to [-pi, pi].
        const bool  rotation      = is_joint_rotation_axis(axis);
        const float unbounded_min = rotation ? -glm::pi<float>() : std::numeric_limits<float>::lowest();
        const float unbounded_max = rotation ?  glm::pi<float>() : std::numeric_limits<float>::max();

        Constraint_axis_limit& limit = m_limits[axis];
        limit.limited = (get_value(limit_property[axis]) == Joint_axis_limit::limited);
        limit.min = (get_value_source(limit_min_property[axis].get()) == erhe::property::Value_source::default_value)
            ? unbounded_min
            : get_value(limit_min_property[axis]);
        limit.max = (get_value_source(limit_max_property[axis].get()) == erhe::property::Value_source::default_value)
            ? unbounded_max
            : get_value(limit_max_property[axis]);
        const float limit_stiffness = get_value(limit_stiffness_property[axis]);
        limit.stiffness = (limit_stiffness > 0.0f) ? std::optional<float>{limit_stiffness} : std::optional<float>{};
        limit.damping   = get_value(limit_damping_property[axis]);

        const Joint_axis_drive drive_mode = get_value(drive_property[axis]);
        if (drive_mode == Joint_axis_drive::acceleration) {
            acceleration_drive = true;
        }
        const float drive_stiffness = get_value(drive_stiffness_property[axis]);
        const float drive_max_force = get_value(drive_max_force_property[axis]);
        Constraint_axis_drive& drive = m_drives[axis];
        drive.enabled             = (drive_mode != Joint_axis_drive::off);
        drive.use_position_target = (drive_stiffness > 0.0f);
        drive.position_target     = get_value(drive_position_target_property[axis]);
        drive.velocity_target     = get_value(drive_velocity_target_property[axis]);
        drive.stiffness           = drive_stiffness;
        drive.damping             = get_value(drive_damping_property[axis]);
        // Zero is the unlimited drive force: a drive that applies no force is
        // Joint_axis_drive::off.
        drive.max_force = (drive_max_force > 0.0f) ? drive_max_force : std::numeric_limits<float>::infinity();
    }
    if (acceleration_drive && !m_warned_acceleration_drive) {
        m_warned_acceleration_drive = true;
        log_physics->warn(
            "Joint settings '{}': acceleration mode drives are not supported; treating as force mode",
            get_name()
        );
    }
}

auto Physics_joint_settings::is_axis_fixed(const std::size_t axis) const -> bool
{
    const Constraint_axis_limit& limit = m_limits[axis];
    return limit.limited && ((limit.max - limit.min) <= Joint_reach::c_fixed_axis_epsilon);
}

} // namespace erhe::physics
