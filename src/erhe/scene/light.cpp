// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/scene/light.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/log.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/verify.hpp"

#include <gsl/gsl>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace erhe::scene
{

Light::Light(const std::string_view name)
    : Node{name}
{
    node_data.flag_bits |= (Node_flag_bit::is_light | Node_flag_bit::is_transform);
}

Light::~Light() noexcept
{
}

auto Light::node_type() const -> const char*
{
    return "Light";
}

auto Light::projection(const Camera& camera) const -> Projection
{
    using glm::vec3;
    using glm::mat4;

    // View distance is radius of the view camera bounding volume
    const float r = camera.projection()->z_far;

    // Directional light uses a cube surrounding the view camera bounding box as projection frustum
    return Projection
    {
        .projection_type = Projection::Type::orthogonal,
        .z_near          = 0.0f,
        .z_far           = 2.0f * r,
        .ortho_width     = 2.0f * r,
        .ortho_height    = 2.0f * r
    };
}

auto Light::projection_transforms(
    const Camera&   view_camera,
    const Viewport& shadow_map_viewport
) const -> Projection_transforms
{
    using vec2 = glm::vec2;
    using vec3 = glm::vec3;
    using vec4 = glm::vec4;
    using mat3 = glm::mat3;
    using mat4 = glm::mat4;

    const Light* light = this;

    // View distance is radius of the view camera bounding volume
    const float r = view_camera.projection()->z_far;

    // Directional light uses a cube surrounding the view camera bounding box as projection frustum
    const Projection light_projection = projection(view_camera);

    // Place light projection camera on the view camera bounding volume using light direction
    const vec3 light_direction       = vec3{light->direction_in_world()};
    const vec3 light_up_vector       = vec3{light->world_from_node() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    const vec3 view_camera_position  = vec3{view_camera.position_in_world()};
    const vec3 light_camera_position = view_camera_position + r * light_direction;

    // Rotation only transform
    const mat3 world_from_light{light->world_from_node()};
    const mat3 light_from_world{light->node_from_world()};

    // Snap camera position to shadow map texture texels
    const vec3 view_camera_position_in_light = light_from_world * view_camera_position;
    const vec2 texel_size{
        (2.0f * r) / static_cast<float>(shadow_map_viewport.width),
        (2.0f * r) / static_cast<float>(shadow_map_viewport.height)
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
    const auto clip_from_light = light_projection.clip_from_node_transform(shadow_map_viewport);

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

    return Projection_transforms{
        clip_from_light,
        Transform{
            clip_from_light.matrix() * snapped_light_from_world,
            snapped_world_from_light * clip_from_light.inverse_matrix()
        }
    };
}

#if 0
    // Transform camera to light space
    //const vec3  bounding_sphere_center_in_light = vec3{node_from_world() * camera->position_in_world()};
    //const float camera_distance = glm::length(
    //    vec3{camera->position_in_world()} - vec3{this->position_in_world()}
    //);

    //const auto      aspect_ratio    = camera_viewport.aspect_ratio();
    //const glm::mat4 clip_from_node  = camera->projection()->get_projection_matrix(aspect_ratio, camera_viewport.reverse_depth);
    //const glm::mat4 node_from_clip  = glm::inverse(clip_from_node);
    //const glm::mat4 world_from_clip = camera->world_from_node() * node_from_clip;
    const glm::mat4 world_from_camera_clip = camera->projection_transforms(camera_viewport).clip_from_world.inverse_matrix();
    const glm::mat4 light_from_camera_clip = node_from_world() * world_from_camera_clip;

    constexpr std::array<glm::vec3, 8> clip_volume = {
        glm::vec3{-1.0f, -1.0f, 0.0f},
        glm::vec3{ 1.0f, -1.0f, 0.0f},
        glm::vec3{ 1.0f,  1.0f, 0.0f},
        glm::vec3{-1.0f,  1.0f, 0.0f},
        glm::vec3{-1.0f, -1.0f, 1.0f},
        glm::vec3{ 1.0f, -1.0f, 1.0f},
        glm::vec3{ 1.0f,  1.0f, 1.0f},
        glm::vec3{-1.0f,  1.0f, 1.0f}
    };

    for (const auto& p : clip_volume)
    {
        const glm::vec3 camera_frustum_point_in_light = glm::vec3{light_from_camera_clip * glm::vec4{p, 1.0f}};
    }

    const auto clip_from_node = m_projection.clip_from_node_transform(viewport);
    return Projection_transforms{
        clip_from_node,
        Transform{
            clip_from_node.matrix() * node_from_world(),
            world_from_node() * clip_from_node.inverse_matrix()
        }
    };
#endif

auto Light::texture_transform(const Transform& clip_from_world) const -> Transform
{
    return Transform{
        texture_from_clip * clip_from_world.matrix(),
        clip_from_world.inverse_matrix() * clip_from_texture
    };
}

auto is_light(const Node* const node) -> bool
{
    if (node == nullptr)
    {
        return false;
    }
    return (node->get_flag_bits() & Node_flag_bit::is_light) == Node_flag_bit::is_light;
}

auto is_light(const std::shared_ptr<Node>& node) -> bool
{
    return is_light(node.get());
}

auto as_light(Node* const node) -> Light*
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if ((node->get_flag_bits() & Node_flag_bit::is_light) == 0)
    {
        return nullptr;
    }
    return reinterpret_cast<Light*>(node);
}

auto as_light(const std::shared_ptr<Node>& node) -> std::shared_ptr<Light>
{
    if (!node)
    {
        return {};
    }
    if ((node->get_flag_bits() & Node_flag_bit::is_light) == 0)
    {
        return {};
    }
    return std::dynamic_pointer_cast<Light>(node);
}

} // namespace erhe::scene
