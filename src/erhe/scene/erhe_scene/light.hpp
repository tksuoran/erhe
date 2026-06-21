#pragma once

#include "erhe_scene/camera.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_math/aabb.hpp"

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
    bool  fit_to_receivers      {true};  // cull casters against the receiver volume (view frustum intersected with receiver bounds) extruded toward the light; refines fit_to_casters and has no effect without it
    bool  fit_to_receivers_hull {true};  // use the tighter convex receiver hull (clipped to the view frustum) instead of a bounding box for the receiver cull volume
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
class Shadow_fit_receiver_cache;     // see light_frustum_fit.hpp
class Shadow_fit_scratch;            // see light_frustum_fit.hpp

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
    // Pointers and spans must outlive the use of these parameters.
    // One world-space AABB per shadow caster; the tight directional fit filters
    // these per light against the shadow caster volume F_shadow before fitting.
    // receiver_world_aabbs holds one world-space AABB per visible receiver
    // (used only when fit_to_receivers is enabled) to build the tighter
    // receiver-aware caster cull volume.
    const Shadow_frustum_fit_settings* fit_settings        {nullptr};
    std::span<const erhe::math::Aabb>  caster_world_aabbs  {};
    std::span<const erhe::math::Aabb>  receiver_world_aabbs{};
    Shadow_frustum_fit_debug_data*     fit_debug_out       {nullptr};
    // Optional cache for the light-independent part of the receiver cull
    // volume build, shared across the lights of one apply() pass (see
    // Shadow_fit_receiver_cache). When null, each fit uses a local cache
    // (correct, but redoes the shared receiver work per light).
    Shadow_fit_receiver_cache*         receiver_cache      {nullptr};
    // Optional persistent scratch buffers for the per-light tight fit (see
    // Shadow_fit_scratch); buffers are cleared (capacity kept) at point of
    // use, so steady-state fits perform no heap allocations. When null, each
    // fit uses a local instance (correct, but allocates per call).
    Shadow_fit_scratch*                fit_scratch         {nullptr};
};

class Light;

class Light_projection_transforms
{
public:
    const Light*  light       {nullptr};
    std::size_t   index       {0}; // index in lights block shader resource (all lights)
    std::size_t   shadow_index{0}; // 2D shadow texture array layer / render pass index (directional + spot); std::size_t max for point lights, which use the cube array instead
    // Dense index (counting only shadow-casting point lights) into the R32F
    // cube-map array used for omnidirectional point-light shadows. std::size_t
    // max for non-point or non-shadow lights. The cube occupies array layers
    // [6*point_shadow_index, 6*point_shadow_index + 6).
    std::size_t   point_shadow_index{0};
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

    // Point lights cast omnidirectional shadows into an R32F cube-map array
    // sampled by direction, so the per-face view-projections are computed in
    // Shadow_renderer at render time, not stored here. This only carries the
    // light world pose (so light_buffer derives the correct light position) and
    // a perspective projection with z_far = range; clip_from_world /
    // texture_from_world are unused by the cube sampling path (left identity).
    [[nodiscard]] auto point_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

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
