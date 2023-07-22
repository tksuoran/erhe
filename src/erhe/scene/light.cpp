// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/scene/light.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene_host.hpp"
#include "erhe/scene/scene_log.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::scene
{

Light::Light(const std::string_view name)
    : Node_attachment{name}
{
}

Light::~Light() noexcept = default;

auto Light::get_static_type() -> uint64_t
{
    return Item_type::node_attachment | Item_type::light;
}

auto Light::get_static_type_name() -> const char*
{
    return "Light";
}

auto Light::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Light::get_type_name() const -> const char*
{
    return get_static_type_name();
}

void Light::handle_item_host_update(
    Item_host* const old_item_host,
    Item_host* const new_item_host
)
{
    const auto shared_this = std::static_pointer_cast<Light>(shared_from_this()); // keep alive

    if (old_item_host) {
        old_item_host->unregister_light(shared_this);
    }
    if (new_item_host) {
        new_item_host->register_light(shared_this);
    }
}

auto Light::projection(const Light_projection_parameters& parameters) const -> Projection
{
    switch (type) {
        case Light_type::directional: return stable_directional_light_projection(parameters);
        case Light_type::spot:        return spot_light_projection              (parameters);
        default: return {};
    }
}

auto Light::stable_directional_light_projection(
    const Light_projection_parameters& parameters
) const -> Projection
{
    ERHE_VERIFY(parameters.view_camera != nullptr);

    using glm::vec3;
    using glm::mat4;

    //// // View distance is radius of the view camera bounding volume
    //// const float r = parameters.view_camera->projection()->z_far;
    const float r = parameters.view_camera->get_shadow_range();

    // Directional light uses a cube surrounding the view camera bounding box as projection frustum
    return Projection{
        .projection_type = Projection::Type::orthogonal,
        .z_near          = 0.0f,
        .z_far           = 2.0f * r,
        .ortho_width     = 2.0f * r,
        .ortho_height    = 2.0f * r
    };
}

auto Light::spot_light_projection(
    const Light_projection_parameters& parameters
) const -> Projection
{
    static_cast<void>(parameters); // TODO ignored for now
    return Projection{
        .projection_type = Projection::Type::perspective,
        .z_near          =   1.0f, // TODO
        .z_far           = 100.0f, // TODO
        .fov_x           = outer_spot_angle,
        .fov_y           = outer_spot_angle
    };
}

auto Light::projection_transforms(
    const Light_projection_parameters& parameters
) const -> Light_projection_transforms
{
    switch (type) {
        case Light_type::directional: {
            return stable_directional_light_projection_transforms(parameters);
            //return this->tight_frustum_fit
            //    ? tight_directional_light_projection_transforms (parameters)
            //    : stable_directional_light_projection_transforms(parameters);
        }

        case Light_type::point: // fallthrough
        case Light_type::spot: {
            return spot_light_projection_transforms(parameters);
        }

        default: {
            return {};
        };
    }
}

auto Light::stable_directional_light_projection_transforms(
    const Light_projection_parameters& parameters
) const -> Light_projection_transforms
{
    // Overview of the stable directional light projection transforms algorithm:
    //
    // - Define bounding sphere around view camare, based on view camera
    //   position (as sphere center) and far plane distance (as sphere radius)
    // - Construct cubic orthogonal projection for light, using view camera far
    //   plane distance as left / rignt / top / bottom
    // - Snap view camera position to light space texels
    //
    // This algorithm produces stable projection, however:
    // - No consideration is given o content (shadow casters, shadow receivers)
    // - Texel size is quite large / A lot of shadow map texture space can be
    //   wasted
    ERHE_VERIFY(parameters.view_camera != nullptr);

    using vec2 = glm::vec2;
    using vec3 = glm::vec3;
    using vec4 = glm::vec4;
    using mat3 = glm::mat3;
    using mat4 = glm::mat4;

    const Node* const light_node = get_node();
    ERHE_VERIFY(light_node != nullptr);

    const Node* const view_camera_node = parameters.view_camera->get_node();
    ERHE_VERIFY(view_camera_node != nullptr);

    //// // View distance is used as radius of the view camera bounding volume
    //// const float r = parameters.view_camera->projection()->z_far;
    const float r = parameters.view_camera->get_shadow_range();

    // Directional light uses a cube surrounding the view camera bounding box as projection frustum
    const Projection light_projection = projection(parameters);

    // Place light projection camera on the view camera bounding volume using light direction
    const vec3 light_direction       = vec3{light_node->direction_in_world()};
    const vec3 light_up_vector       = vec3{light_node->world_from_node() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    const vec3 view_camera_position  = vec3{view_camera_node->position_in_world()};

    // Rotation only transform
    const mat3 world_from_light{light_node->world_from_node()};
    const mat3 light_from_world{light_node->node_from_world()};

    // Snap camera position to shadow map texture texels
    const vec3 view_camera_position_in_light = light_from_world * view_camera_position;
    const vec2 texel_size{
        (2.0f * r) / static_cast<float>(parameters.shadow_map_viewport.width),
        (2.0f * r) / static_cast<float>(parameters.shadow_map_viewport.height)
    };
    const vec2 snap_adjustment{
        std::fmod(view_camera_position_in_light.x, texel_size.x),
        std::fmod(view_camera_position_in_light.y, texel_size.y)
    };
    const vec4 snapped_view_camera_position_in_light{
        view_camera_position_in_light.x - snap_adjustment.x,
        view_camera_position_in_light.y - snap_adjustment.y,
        view_camera_position_in_light.z,
        1.0f
    };

    const vec3 snapped_view_camera_position  = vec3{world_from_light * snapped_view_camera_position_in_light};
    const vec3 snapped_light_camera_position = snapped_view_camera_position + r * light_direction;

    // Snapped transfroms
    const mat4 snapped_world_from_light = erhe::toolkit::create_look_at(
        snapped_light_camera_position, // eye
        snapped_view_camera_position,  // look at
        light_up_vector                // up
    );
    const mat4 snapped_light_from_world = glm::inverse(snapped_world_from_light);

    // Calculate and return projection matrices for light projection camera:
    //  - clip from node   (for light as node)
    //  - clip from world
    const auto clip_from_light = light_projection.clip_from_node_transform(parameters.shadow_map_viewport);

    SPDLOG_LOGGER_TRACE(
        log,
        "view camera position in world {}",
        view_camera_position
    );
    SPDLOG_LOGGER_TRACE(
        log,
        "view camera position in light {}",
        view_camera_position_in_light
    );
    SPDLOG_LOGGER_TRACE(
        log,
        "texel size {}",
        texel_size
    );
    SPDLOG_LOGGER_TRACE(
        log,
        "snap adjustment {}",
        snap_adjustment
    );
    SPDLOG_LOGGER_TRACE(
        log,
        "snapped view camera position in light {}",
        snapped_view_camera_position_in_light
    );
    SPDLOG_LOGGER_TRACE(
        log,
        "snapped view camera position in world {}",
        snapped_view_camera_position
    );

    const Transform world_from_light_camera
    {
        snapped_world_from_light,
        snapped_light_from_world
    };
    const Transform clip_from_world{
        clip_from_light.get_matrix() * snapped_light_from_world,
        snapped_world_from_light * clip_from_light.get_inverse_matrix()
    };
    const Transform texture_from_world{
        texture_from_clip * clip_from_world.get_matrix(),
        clip_from_world.get_inverse_matrix() * clip_from_texture
    };

    return Light_projection_transforms{
        .light                   = this,
        .world_from_light_camera = world_from_light_camera,
        .clip_from_light_camera  = clip_from_light,
        .clip_from_world         = clip_from_world,
        .texture_from_world      = texture_from_world
    };
}

#if 0
[[nodiscard]] auto Light::tight_directional_light_projection_transforms(
    const Light_projection_parameters& parameters
) const -> Light_projection_transforms
{
    // WORK IN PROGRESS - NOT YET FUNCTIONAL
    //
    // Overview of the right directional light projection transforms algorithm:
    // - Start with stable directional light projection transforms
    // - Compute view camera frustum corner points in light space
    // - Compute min and max corner of view camera frustum in light texture space
    using vec2 = glm::vec2;
    using vec3 = glm::vec3;
    using vec4 = glm::vec4;
    using mat3 = glm::mat3;
    using mat4 = glm::mat4;

    ERHE_VERIFY(parameters.view_camera != nullptr);

    const Light* light                        = this;
    const vec3   light_direction              = vec3{light->direction_in_world()};
    const vec3   light_up_vector              = vec3{light->world_from_node() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    const auto   camera_projection_transforms = parameters.view_camera->projection_transforms(parameters.view_camera_viewport);
    const mat4   world_from_view_camera_clip  = camera_projection_transforms.clip_from_world.inverse_matrix();

    // Rotation only transform
    const mat3 world_from_light{light->world_from_node()};
    const mat3 light_from_world{light->node_from_world()};

    const mat4 light_from_view_camera_clip = mat4{light_from_world} * world_from_view_camera_clip;

    constexpr std::array<vec3, 8> clip_space_points = {
        vec3{-1.0f, -1.0f, 0.0f},
        vec3{ 1.0f, -1.0f, 0.0f},
        vec3{ 1.0f,  1.0f, 0.0f},
        vec3{-1.0f,  1.0f, 0.0f},
        vec3{-1.0f, -1.0f, 1.0f},
        vec3{ 1.0f, -1.0f, 1.0f},
        vec3{ 1.0f,  1.0f, 1.0f},
        vec3{-1.0f,  1.0f, 1.0f}
    };

    std::array<vec3, 8> view_frustum_corner_points_in_light = {
        vec3{light_from_view_camera_clip * vec4{clip_space_points[0], 1.0f}},
        vec3{light_from_view_camera_clip * vec4{clip_space_points[1], 1.0f}},
        vec3{light_from_view_camera_clip * vec4{clip_space_points[2], 1.0f}},
        vec3{light_from_view_camera_clip * vec4{clip_space_points[3], 1.0f}},
        vec3{light_from_view_camera_clip * vec4{clip_space_points[4], 1.0f}},
        vec3{light_from_view_camera_clip * vec4{clip_space_points[5], 1.0f}},
        vec3{light_from_view_camera_clip * vec4{clip_space_points[6], 1.0f}},
        vec3{light_from_view_camera_clip * vec4{clip_space_points[7], 1.0f}}
    };

    vec3 min_corner_point = view_frustum_corner_points_in_light.front();
    vec3 max_corner_point = view_frustum_corner_points_in_light.front();
    for (const vec3& p : view_frustum_corner_points_in_light)
    {
        min_corner_point.x = std::min(min_corner_point.x, p.x);
        min_corner_point.y = std::min(min_corner_point.y, p.y);
        min_corner_point.z = std::min(min_corner_point.z, p.z);
        max_corner_point.x = std::max(max_corner_point.x, p.x);
        max_corner_point.y = std::max(max_corner_point.y, p.y);
        max_corner_point.z = std::max(max_corner_point.z, p.z);
    }

    const vec3 view_frustum_size_in_light   = max_corner_point - min_corner_point;
    const vec3 view_frustum_center_in_light = min_corner_point + 0.5f * (view_frustum_size_in_light);

    const Projection light_projection{
        .projection_type = Projection::Type::orthogonal,
        .z_near          = view_frustum_center_in_light.z - 0.5f * std::abs(view_frustum_size_in_light.z),
        .z_far           = view_frustum_center_in_light.z + 0.5f * std::abs(view_frustum_size_in_light.z),
        .ortho_width     = view_frustum_size_in_light.x,
        .ortho_height    = view_frustum_size_in_light.y
    };
    const vec2 texel_size{
        view_frustum_size_in_light.x / static_cast<float>(parameters.shadow_map_viewport.width),
        view_frustum_size_in_light.y / static_cast<float>(parameters.shadow_map_viewport.height)
    };

    const vec2 snap_adjustment{
        std::fmod(view_frustum_center_in_light.x, texel_size.x),
        std::fmod(view_frustum_center_in_light.y, texel_size.y)
    };
    const vec4 snapped_view_camera_position_in_light{
        view_frustum_center_in_light.x - snap_adjustment.x,
        view_frustum_center_in_light.y - snap_adjustment.y,
        view_frustum_center_in_light.z,
        1.0f
    };

    const vec3 snapped_view_camera_position  = vec3{world_from_light * snapped_view_camera_position_in_light};
    const vec3 snapped_light_camera_position = snapped_view_camera_position + 0.5f * std::abs(view_frustum_size_in_light.z) * light_direction;

    // Snapped transfroms
    const mat4 snapped_world_from_light = erhe::toolkit::create_look_at(
        snapped_light_camera_position, // eye
        snapped_view_camera_position,  // look at
        light_up_vector                // up
    );
    const mat4 snapped_light_from_world = glm::inverse(snapped_world_from_light);

    // Calculate and return projection matrices for light projection camera:
    //  - clip from node   (for light as node)
    //  - clip from world
    const auto clip_from_light = light_projection.clip_from_node_transform(parameters.shadow_map_viewport);
    const Transform world_from_light_camera{
        snapped_world_from_light,
        snapped_light_from_world
    };
    const Transform clip_from_world{
        clip_from_light.matrix() * snapped_light_from_world,
        snapped_world_from_light * clip_from_light.inverse_matrix()
    };
    const Transform texture_from_world{
        texture_from_clip * clip_from_world.matrix(),
        clip_from_world.inverse_matrix() * clip_from_texture
    };

    return Light_projection_transforms{
        .light                   = this,
        .world_from_light_camera = world_from_light_camera,
        .clip_from_light_camera  = clip_from_light,
        .clip_from_world         = clip_from_world,
        .texture_from_world      = texture_from_world
    };
}
#endif

[[nodiscard]] auto Light::spot_light_projection_transforms(
    const Light_projection_parameters& parameters
) const -> Light_projection_transforms
{
    const Projection light_projection = projection(parameters);
    const auto clip_from_light_camera = light_projection.clip_from_node_transform(parameters.shadow_map_viewport);
    const Node* const node = get_node();
    ERHE_VERIFY(node != nullptr);

    const Transform clip_from_world{
        clip_from_light_camera.get_matrix() * node->node_from_world(),
        node->world_from_node() * clip_from_light_camera.get_inverse_matrix()
    };
    const Transform texture_from_world{
        texture_from_clip * clip_from_world.get_matrix(),
        clip_from_world.get_inverse_matrix() * clip_from_texture
    };

    return Light_projection_transforms{
        .light                   = this,
        .world_from_light_camera = node->world_from_node_transform(),
        .clip_from_light_camera  = clip_from_light_camera,
        .clip_from_world         = clip_from_world,
        .texture_from_world      = texture_from_world
    };
}

using namespace erhe::toolkit;

auto is_light(const Item* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return test_all_rhs_bits_set(item->get_type(), Item_type::light);
}

auto is_light(const std::shared_ptr<Item>& item) -> bool
{
    return is_light(item.get());
}

auto as_light(Item* const item) -> Light*
{
    if (item == nullptr) {
        return nullptr;
    }
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::light)) {
        return nullptr;
    }
    return reinterpret_cast<Light*>(item);
}

auto as_light(const std::shared_ptr<Item>& item) -> std::shared_ptr<Light>
{
    if (!item) {
        return {};
    }
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::light)) {
        return {};
    }
    return std::static_pointer_cast<Light>(item);
}

auto get_light(const erhe::scene::Node* const node) -> std::shared_ptr<Light>
{
    for (const auto& attachment : node->get_attachments()) {
        auto light = as_light(attachment);
        if (light) {
            return light;
        }
    }
    return {};
}

} // namespace erhe::scene
