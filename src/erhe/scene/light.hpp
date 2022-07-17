#pragma once

#include "erhe/scene/camera.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/transform.hpp"

#include <gsl/gsl>

#include <memory>
#include <string>

namespace erhe::scene
{

enum class Light_type : unsigned int
{
    directional = 0,
    point,
    spot
};

class Light_projection_parameters
{
public:
    const Camera* view_camera{nullptr};
    Viewport      view_camera_viewport;
    Viewport      shadow_map_viewport;
};

class Light;

class Light_projection_transforms
{
public:
    const Light* light{nullptr};
    std::size_t  index; // index in lights block shader resource
    Transform    world_from_light_camera;
    Transform    clip_from_light_camera;
    Transform    clip_from_world;
    Transform    texture_from_world;
};

class Light
    : public Node
{
public:
    using Type = Light_type;

    static constexpr const char* c_type_strings[] =
    {
        "Directional",
        "Point",
        "Spot"
    };

    explicit Light(const std::string_view name);
    ~Light() noexcept override;

    [[nodiscard]] auto node_type            () const -> const char* override;
    [[nodiscard]] auto projection           (const Light_projection_parameters& parameters) const -> Projection;
    [[nodiscard]] auto projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

    Type      type             {Type::directional};
    glm::vec3 color            {1.0f, 1.0f, 1.0f};
    float     intensity        {1.0f};
    float     range            {100.0f}; // TODO projection far?
    float     inner_spot_angle {glm::pi<float>() * 0.4f};
    float     outer_spot_angle {glm::pi<float>() * 0.5f};
    bool      cast_shadow      {true};
    bool      tight_frustum_fit{false};

private:
    [[nodiscard]] auto stable_directional_light_projection(const Light_projection_parameters& parameters) const -> Projection;
    [[nodiscard]] auto spot_light_projection              (const Light_projection_parameters& parameters) const -> Projection;

    [[nodiscard]] auto tight_directional_light_projection_transforms(
        const Light_projection_parameters& parameters
    ) const -> Light_projection_transforms;
    [[nodiscard]] auto stable_directional_light_projection_transforms(
        const Light_projection_parameters& parameters
    ) const -> Light_projection_transforms;
    [[nodiscard]] auto spot_light_projection_transforms(
        const Light_projection_parameters& parameters
    ) const -> Light_projection_transforms;

    static constexpr glm::mat4 texture_from_clip{
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    };

    static constexpr glm::mat4 clip_from_texture{
         2.0f, 0.0f, 0.0f, 0.0f,
         0.0f, 2.0f, 0.0f, 0.0f,
         0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f,-1.0f, 0.0f, 1.0f
    };
};

[[nodiscard]] auto is_light(const Node* const node) -> bool;
[[nodiscard]] auto is_light(const std::shared_ptr<Node>& node) -> bool;
[[nodiscard]] auto as_light(Node* const node) -> Light*;
[[nodiscard]] auto as_light(const std::shared_ptr<Node>& node) -> std::shared_ptr<Light>;

} // namespace erhe::scene
