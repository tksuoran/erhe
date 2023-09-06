#pragma once

#include "erhe_scene/camera.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/scene.hpp" // for Light_layer..
#include "erhe_scene/transform.hpp"

#include <memory>
#include <string>

namespace erhe::scene
{

enum class Light_type : unsigned int {
    directional = 0,
    point,
    spot
};

class Light_projection_parameters
{
public:
    const Camera*        view_camera{nullptr};
    erhe::math::Viewport shadow_map_viewport{};
    ////erhe::math::Viewport view_camera_viewport;
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

class Light : public erhe::Item<Item_base, Node_attachment, Light>
{
public:
    using Type = Light_type;

    static constexpr const char* c_type_strings[] = {
        "Directional",
        "Point",
        "Spot"
    };

    explicit Light(const Light&);
    Light& operator=(const Light&);
    ~Light() noexcept override;

    explicit Light(const std::string_view name);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Light"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    // Implements Node_attachment
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    // Public API
    [[nodiscard]] auto projection           (const Light_projection_parameters& parameters) const -> Projection;
    [[nodiscard]] auto projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

    Type        type             {Type::directional};
    glm::vec3   color            {1.0f, 1.0f, 1.0f};
    float       intensity        {1.0f};
    float       range            {100.0f}; // TODO projection far?
    float       inner_spot_angle {glm::pi<float>() * 0.4f};
    float       outer_spot_angle {glm::pi<float>() * 0.5f};
    bool        cast_shadow      {true};
    bool        tight_frustum_fit{false};
    std::size_t layer_id         {};

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

[[nodiscard]] auto is_light(const Item_base* item) -> bool;
[[nodiscard]] auto is_light(const std::shared_ptr<Item_base>& item) -> bool;

auto get_light(const erhe::scene::Node* node) -> std::shared_ptr<Light>;

} // namespace erhe::scene
