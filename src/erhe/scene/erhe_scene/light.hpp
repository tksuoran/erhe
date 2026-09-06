#pragma once

#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_property/dependency_property.hpp"

#include <limits>
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

// Enumerator table for Light_type properties (labels match Light::c_type_strings).
extern const erhe::property::Enum_info c_light_type_enum_info;

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

// Orthonormal world-space frame of a light, derived from the light node world
// transform (Light::get_light_frame()).
//
// glTF KHR_lights_punctual: a light inherits the orientation of its node;
// position and scale are ignored "except for their effect on the inherited node
// orientation". So node scale must still be applied when the node axes are
// transformed to world space (non-uniform scale reorients them, negative scale
// mirrors them), but it must not survive into the light frame itself: the
// direction is normalized and the frame is orthonormalized. Everything that
// builds a light space - the light camera pose, the shadow projection frustum,
// the light-space basis of the shadow frustum fit, the shaded light direction -
// uses this frame instead of the raw node transform. Using the raw transform
// makes a scaled light node skew light space: light-space extents and texel
// sizes are no longer in world units and the near/far placement is off by the
// scale factor, which is what breaks the directional shadow frustum fit.
class Light_frame
{
public:
    glm::vec3 position {0.0f, 0.0f, 0.0f}; // light node world position
    glm::vec3 direction{0.0f, 0.0f, 1.0f}; // unit; node +Z in world = direction from the lit surface toward the light
    glm::vec3 up       {0.0f, 1.0f, 0.0f}; // unit; node +Y in world, orthogonalized against direction
    glm::vec3 right    {1.0f, 0.0f, 0.0f}; // unit; cross(up, direction) - always right-handed, even for a mirrored node
    glm::mat4 world_from_light{1.0f};      // rigid: (right, up, direction, position)
    glm::mat4 light_from_world{1.0f};      // inverse of world_from_light
};

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

    // True when Light_projections::apply() gave this light a shadow layer
    // (a 2D array layer or a point cube). Shadow-casting lights beyond the
    // shadow limits (Light_count_limits) are not shadow-mapped: they get no
    // shadow pass and are shaded unshadowed like non-shadow lights.
    [[nodiscard]] auto is_shadow_mapped() const -> bool
    {
        return
            (shadow_index       != std::numeric_limits<std::size_t>::max()) ||
            (point_shadow_index != std::numeric_limits<std::size_t>::max());
    }

    Projection    projection{};    // resolved projection; the shadow pass must rasterize with this so it matches clip_from_world / texture_from_world
    Trs_transform world_from_light_camera{};
    Transform     clip_from_light_camera{};
    Transform     clip_from_world{};
    Transform     texture_from_world{};
};

// A light prim (doc/usd-compatibility-plan.md C5, UsdLux): an `Xformable`
// with its own transform, name and children, and a child prim of its parent.
// The `light_type` enumeration picks the UsdLux schema the writer emits; a
// light type that needs properties of its own gets a class of its own then.
class Light : public erhe::Item<Item_base, Xformable, Light, erhe::Item_kind::clone_using_custom_clone_constructor>
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
    Light(const Light& src, erhe::for_clone);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Light"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return Xformable::get_static_type() | erhe::Item_type::light; }

    // Overrides Typed: the class fixes the token. It is the erhe class token;
    // the USD writer maps light_type to the UsdLux typeName.
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Light"; }

    // Overrides Xformable: registers / unregisters the light with the scene's
    // light layer on top of the node registration the base does.
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    // The light itself: the transitional accessor every consumer that reads
    // "the node of this light" still spells, kept while the U steps of
    // doc/usd-compatibility-plan.md retire it.
    [[nodiscard]] auto get_node()       -> Node*       { return this; }
    [[nodiscard]] auto get_node() const -> const Node* { return this; }

    // Public API
    [[nodiscard]] auto projection           (const Light_projection_parameters& parameters) const -> Projection;
    [[nodiscard]] auto projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

    // Whether this light contributes to rendering at all this frame. A point
    // light's range is the distance its light reaches, so range <= 0 means it
    // reaches nowhere: it emits no light and is excluded from the rendered light
    // set entirely (no illumination, no shadow). Such a range also cannot define
    // a valid shadow cube far plane (z_far = range must exceed z_near).
    // All lights are gated by non-black color and non-zero intensity. This is the single
    // source of truth consulted by the light-layer partition, the light buffer,
    // and the shadow renderer, so they always agree on which lights participate.
    [[nodiscard]] auto is_active() const -> bool
    {
        const Type      type             = get_light_type();
        const glm::vec3 color            = get_color();
        return
            (
                (color.r > 0) || (color.g > 0) || (color.b > 0)
            ) &&
            (get_intensity() > 0.0f) &&
            (
                (type != Type::point) || (get_range() > 0.0f)
            ) &&
            (
                (type != Type::spot) || (get_outer_spot_angle() > 0.0f)
            );
    }

    // Whether this light should cast a (rendered) shadow this frame: it must be
    // active and have its shadow flag set.
    [[nodiscard]] auto casts_shadow() const -> bool
    {
        return is_active() && get_cast_shadow();
    }

    // The color the renderer illuminates with: color (acting as tint) modulated
    // by the blackbody chromaticity of `temperature` when temperature > 0,
    // plain `color` otherwise.
    [[nodiscard]] auto get_effective_color() const -> glm::vec3;

    // Chromaticity of a blackbody radiator at the given correlated color
    // temperature (Kelvin), as linear sRGB normalized so the brightest channel
    // is 1 (temperature controls hue only; `intensity` stays the single
    // brightness control). Input is clamped to [1000 K, 15000 K].
    [[nodiscard]] static auto blackbody_color(float temperature_kelvin) -> glm::vec3;

    // Solid angle the light emits into: 4*pi for point lights,
    // 2*pi*(1 - cos(outer_spot_angle/2)) for spot lights, 0 for directional
    // lights (parallel light has no meaningful emission solid angle).
    [[nodiscard]] auto get_solid_angle() const -> float;

    // Photometric flux (lumens) <-> intensity (candela) conversions for point
    // and spot lights: flux = intensity * solid angle. Note that a spot
    // light's flux therefore changes with outer_spot_angle at constant
    // intensity. For directional lights these pass `intensity` (lux) through
    // unconverted.
    [[nodiscard]] auto get_luminous_flux() const -> float;
    void set_luminous_flux(float lumens);

    // Orthonormal world-space frame of the light; see Light_frame. Derived
    // from the light prim's own world transform.
    [[nodiscard]] auto get_light_frame() const -> Light_frame;

    // Registered properties (erhe::property, doc/property-system.md
    // section 4.3). The authored light state lives in the item's property
    // store; every property shares one changed callback that re-resolves
    // the scene's light set (D19), so no writer has to notify by hand.
    static const erhe::property::Property<Light_type> light_type_property;
    // Color acts as a tint; see get_effective_color().
    static const erhe::property::Property<glm::vec3>  color_property;
    // Photometric intensity, matching glTF KHR_lights_punctual units:
    // lux (lm/m^2, illuminance) for directional lights, candela (lm/sr) for
    // point and spot lights. Scenes authored with arbitrary units keep
    // working; the units only give imported/exported content and the
    // photometric helpers a consistent meaning.
    static const erhe::property::Property<float>      intensity_property;
    // Correlated color temperature in Kelvin; 0 disables the temperature
    // contribution (get_effective_color() then returns the color unmodified).
    static const erhe::property::Property<float>      temperature_property;
    static const erhe::property::Property<float>      range_property;
    static const erhe::property::Property<float>      inner_spot_angle_property;
    static const erhe::property::Property<float>      outer_spot_angle_property;
    static const erhe::property::Property<bool>       cast_shadow_property;
    // Derived rows (D26 computed properties): `flux` is intensity times
    // the emission solid angle and its setter writes the intensity, so an
    // undoable edit of it records the intensity; `blackbody` is the
    // chromaticity of `temperature`, read-only.
    static const erhe::property::Property<float>      flux_property;
    static const erhe::property::Property<glm::vec3>  blackbody_property;

    [[nodiscard]] auto get_light_type      () const -> Type      { return get_value(light_type_property); }
    [[nodiscard]] auto get_color           () const -> glm::vec3 { return get_value(color_property); }
    [[nodiscard]] auto get_intensity       () const -> float     { return get_value(intensity_property); }
    [[nodiscard]] auto get_temperature     () const -> float     { return get_value(temperature_property); }
    [[nodiscard]] auto get_range           () const -> float     { return get_value(range_property); }
    [[nodiscard]] auto get_inner_spot_angle() const -> float     { return get_value(inner_spot_angle_property); }
    [[nodiscard]] auto get_outer_spot_angle() const -> float     { return get_value(outer_spot_angle_property); }
    [[nodiscard]] auto get_cast_shadow     () const -> bool      { return get_value(cast_shadow_property); }

    void set_light_type      (Type value)             { set_value(light_type_property, value); }
    void set_color           (const glm::vec3& value) { set_value(color_property, value); }
    void set_intensity       (float value)            { set_value(intensity_property, value); }
    void set_temperature     (float value)            { set_value(temperature_property, value); }
    void set_range           (float value)            { set_value(range_property, value); }
    void set_inner_spot_angle(float value)            { set_value(inner_spot_angle_property, value); }
    void set_outer_spot_angle(float value)            { set_value(outer_spot_angle_property, value); }
    void set_cast_shadow     (bool value)             { set_value(cast_shadow_property, value); }

    // Resolver output (light layer), not authored state: not a property.
    std::size_t layer_id{};

private:
    // Changed callback of every Light property (D19): notifies the
    // Scene_host (Scene_host::on_light_changed) so the scene's resolved
    // light set (erhe::scene_renderer::Light_set) is re-resolved.
    // Registration / unregistration (attach / detach) notifies on its own.
    void notify_changed();
    static void on_light_property_changed(erhe::property::Dependency_object& object, const erhe::property::Property_changed_args& args);

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

// The one light of a prim: the prim itself when it is a Light, else its first
// Light child.
[[nodiscard]] auto get_light(const std::shared_ptr<erhe::Item_base>& item) -> std::shared_ptr<Light>;
[[nodiscard]] auto get_light(const erhe::Hierarchy* item) -> std::shared_ptr<Light>;

} // namespace erhe::scene
