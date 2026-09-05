#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtc/constants.hpp>

namespace erhe::scene {

namespace {

using erhe::property::Dependency_object;
using erhe::property::Property;
using erhe::property::Property_flags;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;
using Type = Projection::Type;

const erhe::property::Owner_type c_owner = Camera::property_owner_type();

// ERHE_camera writes the complete projection, exposure and shadow_range as
// typed fields on every export, so none of them needs a property value
// entry of its own (Property_flags::native_gltf).
constexpr uint32_t c_native = Property_flags::serialize | Property_flags::native_gltf;
constexpr std::string_view c_group = "Projection";

// The visible_when callbacks read the camera's projection mirror; they are
// evaluated on Camera objects only (a holder of Camera values lists them by
// its own value, doc/property-system.md D30).
auto projection_type_of(const Dependency_object& object) -> Type
{
    return static_cast<const Camera&>(object).projection()->projection_type;
}

auto is_perspective(const Dependency_object& object) -> bool
{
    const Type type = projection_type_of(object);
    return (type == Type::perspective) || (type == Type::perspective_xr) || (type == Type::perspective_horizontal) || (type == Type::perspective_vertical);
}
auto uses_fov_x    (const Dependency_object& object) -> bool { const Type t = projection_type_of(object); return (t == Type::perspective) || (t == Type::perspective_horizontal); }
auto uses_fov_y    (const Dependency_object& object) -> bool { const Type t = projection_type_of(object); return (t == Type::perspective) || (t == Type::perspective_vertical); }
auto uses_fov_sides(const Dependency_object& object) -> bool { return projection_type_of(object) == Type::perspective_xr; }
auto uses_width    (const Dependency_object& object) -> bool { const Type t = projection_type_of(object); return (t == Type::orthogonal) || (t == Type::orthogonal_horizontal) || (t == Type::orthogonal_rectangle); }
auto uses_height   (const Dependency_object& object) -> bool { const Type t = projection_type_of(object); return (t == Type::orthogonal) || (t == Type::orthogonal_vertical) || (t == Type::orthogonal_rectangle); }
auto uses_corner   (const Dependency_object& object) -> bool { return projection_type_of(object) == Type::orthogonal_rectangle; }
auto uses_frustum  (const Dependency_object& object) -> bool { return projection_type_of(object) == Type::generic_frustum; }

auto angle(const std::string_view name, float Projection::*member, const float min, const float max, const std::string_view label, const Property_ui::Visible_when visible_when) -> Property<float>
{
    return Property<float>::register_property(
        name, c_owner,
        Property_metadata{
            .default_value = Projection{}.*member,
            .inherits      = true,
            .flags         = c_native,
            .ui            = Property_ui{.min = min, .max = max, .presentation = Property_ui::Presentation::angle_degrees, .group = c_group, .label = label, .visible_when = visible_when}
        }
    );
}

auto extent(const std::string_view name, float Projection::*member, const std::string_view label, const Property_ui::Visible_when visible_when, const std::string_view tooltip = {}) -> Property<float>
{
    return Property<float>::register_property(
        name, c_owner,
        Property_metadata{
            .default_value = Projection{}.*member,
            .inherits      = true,
            .flags         = c_native,
            .ui            = Property_ui{.min = 0.0f, .max = 1000.0f, .presentation = Property_ui::Presentation::slider, .logarithmic = true, .group = c_group, .tooltip = tooltip, .label = label, .visible_when = visible_when}
        }
    );
}

auto log_slider(const float min, const float max, const std::string_view label, const std::string_view tooltip = {}) -> Property_ui
{
    return Property_ui{.min = min, .max = max, .presentation = Property_ui::Presentation::slider, .logarithmic = true, .tooltip = tooltip, .label = label};
}

constexpr float c_pi      = glm::pi<float>();
constexpr float c_half_pi = glm::half_pi<float>();

} // anonymous namespace

const Property<Type> Camera::projection_type_property = Property<Type>::register_property(
    "projection_type", c_owner, c_projection_type_enum_info,
    Property_metadata{
        .default_value = erhe::property::make_value(Projection{}.projection_type),
        .inherits      = true,
        .flags         = c_native,
        .ui            = Property_ui{.group = c_group, .label = "Type"}
    }
);
const Property<float> Camera::fov_x_property     = angle("fov_x", &Projection::fov_x, 0.0f,       c_pi,      "Fov X",     uses_fov_x);
const Property<float> Camera::fov_y_property     = angle("fov_y", &Projection::fov_y, 0.0f,       c_pi,      "Fov Y",     uses_fov_y);
const Property<float> Camera::fov_left_property  = angle("fov_left", &Projection::fov_left, -c_half_pi, c_half_pi, "Fov Left",  uses_fov_sides);
const Property<float> Camera::fov_right_property = angle("fov_right", &Projection::fov_right, -c_half_pi, c_half_pi, "Fov Right", uses_fov_sides);
const Property<float> Camera::fov_up_property    = angle("fov_up", &Projection::fov_up, -c_half_pi, c_half_pi, "Fov Up",    uses_fov_sides);
const Property<float> Camera::fov_down_property  = angle("fov_down", &Projection::fov_down, -c_half_pi, c_half_pi, "Fov Down",  uses_fov_sides);
const Property<float> Camera::ortho_left_property     = extent("ortho_left", &Projection::ortho_left, "Left",   uses_corner);
const Property<float> Camera::ortho_width_property    = extent("ortho_width", &Projection::ortho_width, "Width",  uses_width);
const Property<float> Camera::ortho_bottom_property   = extent("ortho_bottom", &Projection::ortho_bottom, "Bottom", uses_corner);
const Property<float> Camera::ortho_height_property   = extent("ortho_height", &Projection::ortho_height, "Height", uses_height);
const Property<float> Camera::frustum_left_property   = extent("frustum_left", &Projection::frustum_left, "Frustum Left",   uses_frustum);
const Property<float> Camera::frustum_right_property  = extent("frustum_right", &Projection::frustum_right, "Frustum Right",  uses_frustum);
const Property<float> Camera::frustum_bottom_property = extent("frustum_bottom", &Projection::frustum_bottom, "Frustum Bottom", uses_frustum);
const Property<float> Camera::frustum_top_property    = extent("frustum_top", &Projection::frustum_top, "Frustum Top",    uses_frustum);
const Property<float> Camera::z_near_property = extent("z_near", &Projection::z_near, "Z Near", {});
const Property<float> Camera::z_far_property  = extent("z_far", &Projection::z_far, "Z Far",  {}, "Stays the depth hint for shadow fitting and the gizmo while Infinite Z Far is set");
const Property<bool> Camera::infinite_z_far_property = Property<bool>::register_property(
    "infinite_z_far", c_owner,
    Property_metadata{
        .default_value = false,
        .inherits      = true,
        .flags         = c_native,
        .ui            = Property_ui{.group = c_group, .tooltip = "Far plane at infinity (perspective projections only)", .label = "Infinite Z Far", .visible_when = is_perspective}
    }
);
const Property<float> Camera::exposure_property = Property<float>::register_property(
    "exposure", c_owner, Property_metadata{.default_value = 1.0f, .inherits = true, .flags = c_native, .ui = log_slider(0.0f, 800000.0f, "Exposure")}
);
const Property<float> Camera::shadow_range_property = Property<float>::register_property(
    "shadow_range", c_owner, Property_metadata{.default_value = 22.0f, .inherits = true, .flags = c_native, .ui = log_slider(1.0f, 1000.0f, "Shadow Range", "Radius of the bounding sphere the directional shadow fit covers around the camera")}
);

Camera::Camera()                         = default;
Camera::Camera(const Camera&)            = default;
Camera::~Camera() noexcept               = default;

Camera::Camera(const std::string_view name)
    : Item{name}
{
}

Camera::Camera(const Camera& src, erhe::for_clone)
    : Item        {src, erhe::for_clone{}} // the property entries copy with the base (D10)
    , m_projection{src.m_projection}
{
}

void Camera::on_property_changed(const erhe::property::Property_changed_args& args)
{
    if (erhe::property::is_owner_type_or_descendant(c_owner, args.property.get_owner_type())) {
        refresh_projection_mirror();
    }
}

void Camera::refresh_projection_mirror()
{
    m_projection.projection_type = get_value(projection_type_property);
    m_projection.z_near          = get_value(z_near_property);
    m_projection.z_far           = get_value(z_far_property);
    m_projection.infinite_z_far  = get_value(infinite_z_far_property);
    m_projection.fov_x           = get_value(fov_x_property);
    m_projection.fov_y           = get_value(fov_y_property);
    m_projection.fov_left        = get_value(fov_left_property);
    m_projection.fov_right       = get_value(fov_right_property);
    m_projection.fov_up          = get_value(fov_up_property);
    m_projection.fov_down        = get_value(fov_down_property);
    m_projection.ortho_left      = get_value(ortho_left_property);
    m_projection.ortho_width     = get_value(ortho_width_property);
    m_projection.ortho_bottom    = get_value(ortho_bottom_property);
    m_projection.ortho_height    = get_value(ortho_height_property);
    m_projection.frustum_left    = get_value(frustum_left_property);
    m_projection.frustum_right   = get_value(frustum_right_property);
    m_projection.frustum_bottom  = get_value(frustum_bottom_property);
    m_projection.frustum_top     = get_value(frustum_top_property);
}

void Camera::set_projection(const Projection& projection)
{
    set_projection_type(projection.projection_type);
    set_z_near         (projection.z_near);
    set_z_far          (projection.z_far);
    set_infinite_z_far (projection.infinite_z_far);
    set_fov_x          (projection.fov_x);
    set_fov_y          (projection.fov_y);
    set_fov_left       (projection.fov_left);
    set_fov_right      (projection.fov_right);
    set_fov_up         (projection.fov_up);
    set_fov_down       (projection.fov_down);
    set_ortho_left     (projection.ortho_left);
    set_ortho_width    (projection.ortho_width);
    set_ortho_bottom   (projection.ortho_bottom);
    set_ortho_height   (projection.ortho_height);
    set_frustum_left   (projection.frustum_left);
    set_frustum_right  (projection.frustum_right);
    set_frustum_bottom (projection.frustum_bottom);
    set_frustum_top    (projection.frustum_top);
}

void Camera::handle_item_host_update(Item_host* const old_item_host, Item_host* const new_item_host)
{
    const auto shared_this = std::static_pointer_cast<Camera>(shared_from_this()); // keep alive

    Scene_host* old_scene_host = static_cast<Scene_host*>(old_item_host);
    Scene_host* new_scene_host = static_cast<Scene_host*>(new_item_host);

    if (old_scene_host != nullptr) {
        old_scene_host->unregister_camera(shared_this);
    }
    if (new_scene_host != nullptr) {
        new_scene_host->register_camera(shared_this);
    }
}

auto Camera::projection_transforms(
    const erhe::math::Viewport&               viewport,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions
) const -> Camera_projection_transforms
{
    const auto clip_from_node = m_projection.clip_from_node_transform(viewport, reverse_depth, depth_range, conventions);
    const Node* node = get_node();
    ERHE_VERIFY(node != nullptr);
    return Camera_projection_transforms{
        .clip_from_camera = clip_from_node,
        .clip_from_world = Transform{
            clip_from_node.get_matrix() * node->node_from_world(),
            node->world_from_node()     * clip_from_node.get_inverse_matrix()
        }
    };
}

auto Camera::projection() const -> const Projection*
{
    return &m_projection;
}

auto Camera::get_projection_scale() const -> float
{
    return m_projection.get_scale();
}

} // namespace erhe::scene
