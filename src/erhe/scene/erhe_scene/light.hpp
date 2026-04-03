#pragma once

#include "erhe_scene/camera.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/transform.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace erhe::scene {

enum class Light_type : unsigned int {
    directional = 0,
    point,
    spot
};

class Light_projection_parameters
{
public:
    const Camera*                      view_camera         {nullptr};
    erhe::math::Viewport               main_camera_viewport{};
    erhe::math::Viewport               shadow_map_viewport {};
    bool                               reverse_depth       {true};
    erhe::math::Depth_range            depth_range         {erhe::math::Depth_range::zero_to_one};
    erhe::math::Coordinate_conventions conventions;
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

    explicit Light(std::string_view name);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Light"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node_attachment | erhe::Item_type::light; }

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

    //// [[nodiscard]] auto tight_directional_light_projection_transforms(
    ////     const Light_projection_parameters& parameters
    //// ) const -> Light_projection_transforms;
    [[nodiscard]] auto stable_directional_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

    [[nodiscard]] auto spot_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

    // Maps clip space to [0,1] texture space.
    // For zero_to_one:          z is already in [0,1], identity for z
    // For negative_one_to_one:  z is in [-1,1], needs scale+bias
    [[nodiscard]] static auto get_texture_from_clip(erhe::math::Depth_range depth_range, const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}) -> glm::mat4;
    [[nodiscard]] static auto get_clip_from_texture(erhe::math::Depth_range depth_range, const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}) -> glm::mat4;
};

} // namespace erhe::scene
