#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/light_frustum_fit.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <memory>

namespace erhe::graphics {
    class Render_command_encoder;
    class Texture;
    class Texture_heap;
}
namespace erhe::primitive {
    class Material;
}

namespace erhe::scene_renderer {

class Shadow_renderer;

class Light_struct
{
public:
    std::size_t clip_from_world;                // mat4
    std::size_t texture_from_world;             // mat4
    std::size_t world_from_texture;             // mat4
    std::size_t position_and_inner_spot_cos;    // vec4 (vec3, float)
    std::size_t direction_and_outer_spot_cos;   // vec4 (vec3, float)
    std::size_t radiance_and_range;             // vec4 (float, float, padding, padding )
    // shadow_index = the dense shadow-array-layer that the shadow
    // renderer wrote this light's shadow map into. Different from the
    // light's UBO `index` slot whenever a preceding type bucket has
    // non-shadow lights (those skip the shadow array layer). Read by
    // the fragment shader as `array_layer = float(shadow_index.x)`.
    // packed[1..3] reserved for future per-light shadow metadata.
    std::size_t shadow_index_packed;            // uvec4 (uint shadow_index, uvec3 padding)
};

class Light_block
{
public:
    std::size_t  shadow_texture_compare;       // uvec2
    std::size_t  shadow_texture_no_compare;    // uvec2

    std::size_t  directional_light_count;      // uint
    std::size_t  spot_light_count;             // uint
    std::size_t  point_light_count;            // uint
    std::size_t  directional_shadow_count;     // uint - shadow-mapped prefix size for directional lights

    std::size_t  spot_shadow_count;            // uint - shadow-mapped prefix size for spot lights
    std::size_t  point_shadow_count;           // uint - shadow-mapped prefix size for point lights
    std::size_t  brdf_material;                // uint
    std::size_t  reserved_1;                   // uint - pad to vec2 alignment

    std::size_t  brdf_phi_incident_phi;        // vec2

    std::size_t  ambient_light;                // vec4

    Light_struct light;
    std::size_t  light_struct;
};

static constexpr uint32_t c_texture_heap_slot_shadow_compare   {0};
static constexpr uint32_t c_texture_heap_slot_shadow_no_compare{1};
// Color-aspect binding for the Shadow_technique_mode::distance R32F distance
// map. Separate from the two depth-aspect shadow samplers above because a
// color texture cannot bind to a depth-aspect combined-image-sampler.
static constexpr uint32_t c_texture_heap_slot_shadow_distance  {2};

class Light_interface
{
public:
    Light_interface(erhe::graphics::Device& graphics_device, int max_light_count);

    // Returns the dedicated shadow sampler for the named role. The
    // comparison-enabled variant uses the comparison op that matches the
    // engine-wide reverse_depth choice (Device::get_reverse_depth());
    // there is no per-call override because both Vulkan portability subset
    // (MoltenVK) and the immutable-sampler descriptor wiring require the
    // direction to be fixed at engine init.
    [[nodiscard]] auto get_sampler(bool compare) const -> const erhe::graphics::Sampler*;

    std::size_t                     max_light_count;
    erhe::graphics::Shader_resource light_block;
    erhe::graphics::Shader_resource light_control_block;
    erhe::graphics::Shader_resource light_struct;
    Light_block                     offsets;
    std::size_t                     light_index_offset;
    // Per-pass coefficient the distance caster multiplies fwidth() by, =
    // cdd*(1+pcfRadius) (Shadow_technique_mode::distance). 0 for the depth path.
    std::size_t                     shadow_distance_bias_coeff_offset;
    erhe::graphics::Sampler         shadow_sampler_compare;
    erhe::graphics::Sampler         shadow_sampler_no_compare;
};

// Selects camera for which the shadow frustums are fitted
class Light_projections
{
public:
    void apply(
        const std::span<const std::shared_ptr<erhe::scene::Light>>& lights,
        const erhe::scene::Camera*                                  main_camera,
        const erhe::math::Viewport&                                 main_camera_viewport,
        const erhe::math::Viewport&                                 light_texture_viewport,
        const std::shared_ptr<erhe::graphics::Texture>&             in_shadow_map_texture,
        bool                                                        reverse_depth,
        erhe::math::Depth_range                                     depth_range,
        const erhe::math::Coordinate_conventions&                   conventions = erhe::math::Coordinate_conventions{},
        std::span<const erhe::math::Aabb>                           in_caster_world_aabbs = {},
        std::span<const erhe::math::Aabb>                           in_receiver_world_aabbs = {},
        const erhe::scene::Shadow_frustum_fit_settings*             fit_settings = nullptr
    );

    // Warning: Returns pointer to element of member vector. That pointer
    //          should remain stable as long as Light_projections stays
    //          alive.
    [[nodiscard]] auto get_light_projection_transforms_for_light(const erhe::scene::Light* light) -> erhe::scene::Light_projection_transforms*;
    [[nodiscard]] auto get_light_projection_transforms_for_light(const erhe::scene::Light* light) const -> const erhe::scene::Light_projection_transforms*;

    erhe::scene::Light_projection_parameters              parameters;
    std::vector<erhe::scene::Light_projection_transforms> light_projection_transforms;
    std::shared_ptr<erhe::graphics::Texture>              shadow_map_texture;
    // Shadow_technique_mode::distance R32F distance map (the fwidth-biased
    // distances the caster wrote). Null for the depth technique; the receiver
    // then samples shadow_map_texture through the depth samplers instead.
    std::shared_ptr<erhe::graphics::Texture>              shadow_distance_texture;

    // Frame-lifetime backing storage for parameters.fit_debug_out; parallel
    // to light_projection_transforms, and resized (then filled by the fit)
    // only when fit_settings->collect_debug is enabled - with the setting off
    // the fit performs no debug-collection work at all, so the algorithm can
    // be profiled on its own. (parameters' AABB spans are not backed here;
    // they point at caller storage during apply() and are reset before it
    // returns.)
    std::vector<erhe::scene::Shadow_frustum_fit_debug_data> fit_debug_data;

    // Cross-light cache for the light-independent part of the receiver cull
    // volume build (see Shadow_fit_receiver_cache). Invalidated at the start
    // of each apply() pass; buffers keep their capacity across frames.
    erhe::scene::Shadow_fit_receiver_cache fit_receiver_cache;

    // Persistent scratch buffers for the per-light tight fit (see
    // Shadow_fit_scratch). Cleared (capacity kept) by the fit at point of
    // use, so steady-state fits perform no heap allocations.
    erhe::scene::Shadow_fit_scratch fit_scratch;

    //Variant_counts                                        counts{};

    // TODO A bit hacky injection of these parameters..
    float                                                 brdf_phi         {0.0f};
    float                                                 brdf_incident_phi{0.0f};
    std::shared_ptr<erhe::primitive::Material>            brdf_material    {};
};

class Light_buffer
{
public:
    // init_command_buffer must be in recording state. Light_buffer
    // records the fallback-shadow-texture clear into it; the caller
    // must end + submit the cb (and wait) before the texture is sampled.
    Light_buffer(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        Light_interface&                light_interface
    );

    auto update(
        const std::span<const std::shared_ptr<erhe::scene::Light>>& lights,
        const Light_projections*                                    light_projections,
        const glm::vec3&                                            ambient_light
    ) -> erhe::graphics::Ring_buffer_range;

    // Bind the shadow map textures to the s_shadow_compare and
    // s_shadow_no_compare sampler bindings declared in the bind group layout.
    // Callers that read shadows in their fragment shader (e.g. forward
    // renderer) must call this before draw; callers that don't (e.g. shadow
    // renderer, texel renderer) can skip it.
    void bind_shadow_samplers(
        erhe::graphics::Render_command_encoder& encoder,
        const Light_projections*                light_projections
    );

    auto update_control(std::size_t light_index, float shadow_distance_bias_coeff = 0.0f) -> erhe::graphics::Ring_buffer_range;

    void bind_light_buffer  (erhe::graphics::Command_encoder& encoder, const erhe::graphics::Ring_buffer_range& range);
    void bind_control_buffer(erhe::graphics::Command_encoder& encoder, const erhe::graphics::Ring_buffer_range& range);

private:
    erhe::graphics::Device&                  m_graphics_device;
    Light_interface&                         m_light_interface;
    erhe::graphics::Ring_buffer_client       m_light_buffer;
    erhe::graphics::Ring_buffer_client       m_control_buffer;
    std::shared_ptr<erhe::graphics::Texture> m_fallback_shadow_texture;
    // 1x1 R32F color texture bound to s_shadow_distance whenever no distance
    // map is active (depth technique), so the color-aspect binding always has
    // a valid texture. Mirrors m_fallback_shadow_texture for the depth path.
    std::shared_ptr<erhe::graphics::Texture> m_fallback_distance_texture;
};

} // namespace erhe::scene_renderer
