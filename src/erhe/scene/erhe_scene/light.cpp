// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene/light.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtc/constants.hpp>

namespace erhe::scene {

auto Light::get_texture_from_clip(const erhe::math::Depth_range depth_range, const erhe::math::Coordinate_conventions& conventions) -> glm::mat4
{
    // Metal (top-left origin): y needs to be flipped: y_tex = -0.5 * y_ndc + 0.5
    // OpenGL (bottom-left origin): y_tex = 0.5 * y_ndc + 0.5
    const float y_scale = (conventions.framebuffer_origin == erhe::math::Framebuffer_origin::top_left) ? -0.5f : 0.5f;

    if (depth_range == erhe::math::Depth_range::zero_to_one) {
        // z already in [0,1], identity for z
        return glm::mat4{
            0.5f,    0.0f, 0.0f, 0.0f,
            0.0f, y_scale, 0.0f, 0.0f,
            0.0f,    0.0f, 1.0f, 0.0f,
            0.5f,    0.5f, 0.0f, 1.0f
        };
    } else {
        // negative_one_to_one: z in [-1,1], needs scale+bias to [0,1]
        return glm::mat4{
            0.5f,    0.0f, 0.0f, 0.0f,
            0.0f, y_scale, 0.0f, 0.0f,
            0.0f,    0.0f, 0.5f, 0.0f,
            0.5f,    0.5f, 0.5f, 1.0f
        };
    }
}

auto Light::get_clip_from_texture(const erhe::math::Depth_range depth_range, const erhe::math::Coordinate_conventions& conventions) -> glm::mat4
{
    const float y_scale = (conventions.framebuffer_origin == erhe::math::Framebuffer_origin::top_left) ? -2.0f : 2.0f;
    const float y_bias  = (conventions.framebuffer_origin == erhe::math::Framebuffer_origin::top_left) ?  1.0f : -1.0f;

    if (depth_range == erhe::math::Depth_range::zero_to_one) {
        // Inverse of zero_to_one texture_from_clip
        return glm::mat4{
             2.0f,    0.0f, 0.0f, 0.0f,
             0.0f, y_scale, 0.0f, 0.0f,
             0.0f,    0.0f, 1.0f, 0.0f,
            -1.0f,  y_bias, 0.0f, 1.0f
        };
    } else {
        // Inverse of negative_one_to_one texture_from_clip
        return glm::mat4{
             2.0f,    0.0f, 0.0f, 0.0f,
             0.0f, y_scale, 0.0f, 0.0f,
             0.0f,    0.0f, 2.0f, 0.0f,
            -1.0f,  y_bias,-1.0f, 1.0f
        };
    }
}

Light::Light(const Light&)            = default;
Light& Light::operator=(const Light&) = default;
Light::~Light() noexcept              = default;

Light::Light(const std::string_view name)
    : Item{name}
{
}

Light::Light(const Light& src, erhe::for_clone)
    : Item            {src, erhe::for_clone{}}
    , type            {src.type            }
    , color           {src.color           }
    , intensity       {src.intensity       }
    , range           {src.range           }
    , inner_spot_angle{src.inner_spot_angle}
    , outer_spot_angle{src.outer_spot_angle}
    , cast_shadow     {src.cast_shadow     }
    , layer_id        {src.layer_id        }
{
}

void Light::handle_item_host_update(erhe::Item_host* const old_item_host, erhe::Item_host* const new_item_host)
{
    const auto shared_this = std::static_pointer_cast<Light>(shared_from_this()); // keep alive

    Scene_host* old_scene_host = static_cast<Scene_host*>(old_item_host);
    Scene_host* new_scene_host = static_cast<Scene_host*>(new_item_host);

    if (old_scene_host) {
        old_scene_host->unregister_light(shared_this);
    }
    if (new_scene_host) {
        new_scene_host->register_light(shared_this);
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

auto Light::stable_directional_light_projection(const Light_projection_parameters& parameters) const -> Projection
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

auto Light::spot_light_projection(const Light_projection_parameters& parameters) const -> Projection
{
    static_cast<void>(parameters); // TODO ignored for now
    return Projection{
        .projection_type = Projection::Type::perspective,
        .z_near          = 0.04f, // TODO
        .z_far           = range,
        .fov_x           = outer_spot_angle,
        .fov_y           = outer_spot_angle
    };
}

auto Light::projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms
{
    ERHE_PROFILE_FUNCTION();
    switch (type) {
        case Light_type::directional: {
            return directional_light_projection_transforms(parameters);
        }

        case Light_type::point: {
            return point_light_projection_transforms(parameters);
        }

        case Light_type::spot: {
            return spot_light_projection_transforms(parameters);
        }

        default: {
            return {};
        };
    }
}

auto Light::directional_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms
{
    ERHE_PROFILE_FUNCTION();
    const bool use_tight_fit =
        (parameters.fit_settings != nullptr) &&
        parameters.fit_settings->any_tightening_enabled();
    return use_tight_fit
        ? tight_directional_light_projection_transforms (parameters)
        : stable_directional_light_projection_transforms(parameters);
}

auto Light::stable_directional_light_projection_transforms(
    const Light_projection_parameters& parameters
) const -> Light_projection_transforms
{
    ERHE_PROFILE_FUNCTION();
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
    const bool texel_snap = (parameters.fit_settings == nullptr) || parameters.fit_settings->texel_snap;
    const vec3 view_camera_position_in_light = light_from_world * view_camera_position;
    const vec2 texel_size{
        (2.0f * r) / static_cast<float>(parameters.shadow_map_viewport.width),
        (2.0f * r) / static_cast<float>(parameters.shadow_map_viewport.height)
    };
    const vec2 snap_adjustment = texel_snap
        ? vec2{
            std::fmod(view_camera_position_in_light.x, texel_size.x),
            std::fmod(view_camera_position_in_light.y, texel_size.y)
        }
        : vec2{0.0f, 0.0f};
    const vec4 snapped_view_camera_position_in_light{
        view_camera_position_in_light.x - snap_adjustment.x,
        view_camera_position_in_light.y - snap_adjustment.y,
        view_camera_position_in_light.z,
        1.0f
    };

    const vec3 snapped_view_camera_position  = vec3{world_from_light * snapped_view_camera_position_in_light};
    const vec3 snapped_light_camera_position = snapped_view_camera_position + r * light_direction;

    // Snapped transfroms
    const mat4 snapped_world_from_light = erhe::math::create_look_at(
        snapped_light_camera_position, // eye
        snapped_view_camera_position,  // look at
        light_up_vector                // up
    );
    const mat4 snapped_light_from_world = glm::inverse(snapped_world_from_light);

    SPDLOG_LOGGER_TRACE(log, "view camera position in world {}", view_camera_position);
    SPDLOG_LOGGER_TRACE(log, "view camera position in light {}", view_camera_position_in_light);
    SPDLOG_LOGGER_TRACE(log, "texel size {}", texel_size);
    SPDLOG_LOGGER_TRACE(log, "snap adjustment {}", snap_adjustment);
    SPDLOG_LOGGER_TRACE(log, "snapped view camera position in light {}", snapped_view_camera_position_in_light);
    SPDLOG_LOGGER_TRACE(log, "snapped view camera position in world {}", snapped_view_camera_position);

    return assemble_directional_light_projection_transforms(parameters, light_projection, snapped_world_from_light, snapped_light_from_world);
}

auto Light::assemble_directional_light_projection_transforms(
    const Light_projection_parameters& parameters,
    const Projection&                  light_projection,
    const glm::mat4&                   world_from_light_camera,
    const glm::mat4&                   light_camera_from_world
) const -> Light_projection_transforms
{
    ERHE_PROFILE_FUNCTION();
    // Calculate and return projection matrices for light projection camera:
    //  - clip from node   (for light as node)
    //  - clip from world
    const Transform clip_from_light = light_projection.clip_from_node_transform(parameters.shadow_map_viewport, parameters.reverse_depth, parameters.depth_range);

    // TODO Could we use Trs_transform more directly without going through mat4?
    const Trs_transform world_from_light_camera_transform{
        world_from_light_camera,
        light_camera_from_world
    };
    const Transform clip_from_world{
        clip_from_light.get_matrix() * light_camera_from_world,
        world_from_light_camera * clip_from_light.get_inverse_matrix()
    };
    const glm::mat4 tex_from_clip = get_texture_from_clip(parameters.depth_range, parameters.conventions);
    const glm::mat4 clip_from_tex = get_clip_from_texture(parameters.depth_range, parameters.conventions);
    const Transform texture_from_world{
        tex_from_clip * clip_from_world.get_matrix(),
        clip_from_world.get_inverse_matrix() * clip_from_tex
    };

    return Light_projection_transforms{
        .light                   = this,
        .projection              = light_projection,
        .world_from_light_camera = world_from_light_camera_transform,
        .clip_from_light_camera  = clip_from_light,
        .clip_from_world         = clip_from_world,
        .texture_from_world      = texture_from_world
    };
}

auto Light::spot_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms
{
    ERHE_PROFILE_FUNCTION();
    const Projection light_projection = projection(parameters);
    const auto clip_from_light_camera = light_projection.clip_from_node_transform(parameters.shadow_map_viewport, parameters.reverse_depth, parameters.depth_range);
    const Node* const node = get_node();
    ERHE_VERIFY(node != nullptr);

    const Transform clip_from_world{
        clip_from_light_camera.get_matrix() * node->node_from_world(),
        node->world_from_node() * clip_from_light_camera.get_inverse_matrix()
    };
    const glm::mat4 tex_from_clip = get_texture_from_clip(parameters.depth_range, parameters.conventions);
    const glm::mat4 clip_from_tex = get_clip_from_texture(parameters.depth_range, parameters.conventions);
    const Transform texture_from_world{
        tex_from_clip * clip_from_world.get_matrix(),
        clip_from_world.get_inverse_matrix() * clip_from_tex
    };

    return Light_projection_transforms{
        .light                   = this,
        .projection              = light_projection,
        .world_from_light_camera = node->world_from_node_transform(),
        .clip_from_light_camera  = clip_from_light_camera,
        .clip_from_world         = clip_from_world,
        .texture_from_world      = texture_from_world
    };
}

auto Light::point_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms
{
    ERHE_PROFILE_FUNCTION();
    static_cast<void>(parameters);
    const Node* const node = get_node();
    ERHE_VERIFY(node != nullptr);

    // The omnidirectional cube shadow is rasterized per face in Shadow_renderer
    // from six 90-degree perspectives centred on the light position. Only the
    // light pose (for the light position written by light_buffer.cpp) and the
    // far plane (= range) matter here; the receiver samples the cube by
    // direction, so clip_from_world / texture_from_world are unused and kept
    // identity to avoid implying a single-projection shadow lookup.
    const Projection light_projection{
        .projection_type = Projection::Type::perspective,
        .z_near          = 0.05f,
        .z_far           = range,
        .fov_x           = glm::half_pi<float>(),
        .fov_y           = glm::half_pi<float>()
    };

    return Light_projection_transforms{
        .light                   = this,
        .projection              = light_projection,
        .world_from_light_camera = node->world_from_node_transform()
    };
}

} // namespace erhe::scene
