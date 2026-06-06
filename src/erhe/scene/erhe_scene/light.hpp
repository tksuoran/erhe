#pragma once

#include "erhe_scene/camera.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace erhe::scene {

enum class Light_type : unsigned int {
    directional = 0,
    point,
    spot
};

class Shadow_frustum_fit_settings
{
public:
    // Tightening steps - with all of these off the fit is identical to the
    // stable bounding-sphere fit (bounding sphere around the view camera,
    // radius = Camera::get_shadow_range()).
    bool  fit_to_view_frustum   {false}; // fit light-space extents to the main camera view frustum corners
    bool  fit_to_casters        {false}; // fit to the shadow caster convex hull clipped to the extruded shadow volume (F_shadow)
    bool  optimize_rotation     {false}; // rotating calipers roll around the light direction for minimum area coverage
    bool  near_from_main_frustum{false}; // near distance from the F_main plane most facing the light (relies on depth_clamp for closer casters)
    bool  depth_clamp           {false}; // depth-clamp rasterization in the shadow pass

    // Stabilization
    bool  texel_snap            {true};  // snap the light-space box to shadow map texels
    bool  quantize_extents      {false}; // round the light-space box size up to multiples of quantize_step
    float quantize_step         {0.0f};  // world units; 0 derives a step from the stable fit texel size

    // Safety
    bool  cap_by_shadow_range   {true};  // never exceed the stable shadow-range box extents

    // Debug
    bool  collect_debug         {false}; // emit per-step intermediates for debug visualization

    [[nodiscard]] auto any_tightening_enabled() const -> bool
    {
        return fit_to_view_frustum || fit_to_casters;
    }
};

class Shadow_frustum_fit_debug_data; // see light_frustum_fit.hpp

class Light_projection_parameters
{
public:
    const Camera*                      view_camera         {nullptr};
    erhe::math::Viewport               main_camera_viewport{};
    erhe::math::Viewport               shadow_map_viewport {};
    bool                               reverse_depth       {true};
    erhe::math::Depth_range            depth_range         {erhe::math::Depth_range::zero_to_one};
    erhe::math::Coordinate_conventions conventions;

    // Optional tight frustum fit inputs; defaults give the legacy stable fit.
    // Pointers and span must outlive the use of these parameters.
    const Shadow_frustum_fit_settings* fit_settings       {nullptr};
    std::span<const glm::vec3>         caster_world_points{};
    Shadow_frustum_fit_debug_data*     fit_debug_out      {nullptr};
};

class Light;

class Light_projection_transforms
{
public:
    const Light*  light       {nullptr};
    std::size_t   index       {0}; // index in lights block shader resource (all lights)
    std::size_t   shadow_index{0}; // shadow texture array layer / render pass index (equals index when partitioned)
    Projection    projection;      // resolved projection; the shadow pass must rasterize with this so it matches clip_from_world / texture_from_world
    Trs_transform world_from_light_camera;
    Transform     clip_from_light_camera;
    Transform     clip_from_world;
    Transform     texture_from_world;
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
    std::size_t layer_id         {};

private:
    [[nodiscard]] auto stable_directional_light_projection(const Light_projection_parameters& parameters) const -> Projection;
    [[nodiscard]] auto spot_light_projection              (const Light_projection_parameters& parameters) const -> Projection;

    [[nodiscard]] auto directional_light_projection_transforms       (const Light_projection_parameters& parameters) const -> Light_projection_transforms;
    [[nodiscard]] auto stable_directional_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

    // Tight modular fit; defined in light_frustum_fit.cpp
    [[nodiscard]] auto tight_directional_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

    [[nodiscard]] auto spot_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

    // Assembles the Light_projection_transforms for a directional light from
    // the resolved projection and the (snapped) light camera pose. Shared by
    // the stable and tight fit paths.
    [[nodiscard]] auto assemble_directional_light_projection_transforms(
        const Light_projection_parameters& parameters,
        const Projection&                  light_projection,
        const glm::mat4&                   world_from_light_camera,
        const glm::mat4&                   light_camera_from_world
    ) const -> Light_projection_transforms;

    // Maps clip space to [0,1] texture space.
    // For zero_to_one:          z is already in [0,1], identity for z
    // For negative_one_to_one:  z is in [-1,1], needs scale+bias
    [[nodiscard]] static auto get_texture_from_clip(erhe::math::Depth_range depth_range, const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}) -> glm::mat4;
    [[nodiscard]] static auto get_clip_from_texture(erhe::math::Depth_range depth_range, const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}) -> glm::mat4;
};

} // namespace erhe::scene
