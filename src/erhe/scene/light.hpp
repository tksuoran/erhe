#pragma once

#include "erhe/scene/camera.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp" // for Light_layer..
#include "erhe/scene/transform.hpp"

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
    const Camera*                view_camera{nullptr};
    erhe::toolkit::Viewport     shadow_map_viewport{};
    ////erhe::toolkit::Viewport view_camera_viewport;
};

class Light;

class Light_projection_transforms
{
public:
    const Light* light{nullptr};
    std::size_t  index{0}; // index in lights block shader resource
    Transform    world_from_light_camera;
    Transform    clip_from_light_camera;
    Transform    clip_from_world;
    Transform    texture_from_world;
};

class Light
    : public Node_attachment
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

    // Implements Node_attachment
    [[nodiscard]] static auto static_type     () -> uint64_t;
    [[nodiscard]] static auto static_type_name() -> const char*;
    void handle_node_scene_host_update(Scene_host* old_scene_host, Scene_host* new_scene_host) override;

    // Implements Item
    [[nodiscard]] auto get_type () const -> uint64_t override;
    [[nodiscard]] auto type_name() const -> const char* override;

    // Public API
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
    erhe::toolkit::Unique_id<Light_layer>::id_type layer_id{};

private:
    [[nodiscard]] auto stable_directional_light_projection(const Light_projection_parameters& parameters) const -> Projection;
    [[nodiscard]] auto spot_light_projection              (const Light_projection_parameters& parameters) const -> Projection;

    ////[[nodiscard]] auto tight_directional_light_projection_transforms(
    ////    const Light_projection_parameters& parameters
    ////) const -> Light_projection_transforms;
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

[[nodiscard]] auto is_light(const Item* scene_item) -> bool;
[[nodiscard]] auto is_light(const std::shared_ptr<Item>& scene_item) -> bool;
[[nodiscard]] auto as_light(Item* scene_item) -> Light*;
[[nodiscard]] auto as_light(const std::shared_ptr<Item>& scene_item) -> std::shared_ptr<Light>;

auto get_light(const erhe::scene::Node* node) -> std::shared_ptr<Light>;

} // namespace erhe::scene
