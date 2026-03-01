#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_math/viewport.hpp"

#include <memory>

namespace erhe::graphics {
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
};

class Light_block
{
public:
    std::size_t  shadow_texture_compare;    // uvec2
    std::size_t  shadow_texture_no_compare; // uvec2

    std::size_t  directional_light_count;   // uint
    std::size_t  spot_light_count;          // uint
    std::size_t  point_light_count;         // uint
    std::size_t  reserved_1;                // uint

    std::size_t  brdf_material;             // uint
    std::size_t  reserved_2;                // uint
    std::size_t  brdf_phi_incident_phi;     // vec2

    std::size_t  ambient_light;             // vec4

    Light_struct light;
    std::size_t  light_struct;
};

static constexpr uint32_t c_texture_heap_slot_shadow_compare   {0};
static constexpr uint32_t c_texture_heap_slot_shadow_no_compare{1};
static constexpr uint32_t c_texture_heap_slot_count_reserved   {2};

class Light_interface
{
public:
    explicit Light_interface(erhe::graphics::Device& graphics_device);

    [[nodiscard]] auto get_sampler(bool compare) const -> const erhe::graphics::Sampler*;

    std::size_t                     max_light_count;
    erhe::graphics::Shader_resource light_block;
    erhe::graphics::Shader_resource light_control_block;
    erhe::graphics::Shader_resource light_struct;
    Light_block                     offsets;
    std::size_t                     light_index_offset;
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
        const std::shared_ptr<erhe::graphics::Texture>&             in_shadow_map_texture
    );

    // Warning: Returns pointer to element of member vector. That pointer
    //          should remain stable as long as Light_projections stays
    //          alive.
    [[nodiscard]] auto get_light_projection_transforms_for_light(const erhe::scene::Light* light) -> erhe::scene::Light_projection_transforms*;
    [[nodiscard]] auto get_light_projection_transforms_for_light(const erhe::scene::Light* light) const -> const erhe::scene::Light_projection_transforms*;

    erhe::scene::Light_projection_parameters              parameters;
    std::vector<erhe::scene::Light_projection_transforms> light_projection_transforms;
    std::shared_ptr<erhe::graphics::Texture>              shadow_map_texture;

    // TODO A bit hacky injection of these parameters..
    float                                                 brdf_phi         {0.0f};
    float                                                 brdf_incident_phi{0.0f};
    std::shared_ptr<erhe::primitive::Material>            brdf_material    {};
};

class Light_buffer
{
public:
    Light_buffer(erhe::graphics::Device& graphics_device, Light_interface& light_interface);

    auto update(
        const std::span<const std::shared_ptr<erhe::scene::Light>>& lights,
        const Light_projections*                                    light_projections,
        const glm::vec3&                                            ambient_light,
        erhe::graphics::Texture_heap&                               texture_heap
    ) -> erhe::graphics::Ring_buffer_range;

    auto update_control(std::size_t light_index) -> erhe::graphics::Ring_buffer_range;

    void bind_light_buffer  (erhe::graphics::Command_encoder& encoder, const erhe::graphics::Ring_buffer_range& range);
    void bind_control_buffer(erhe::graphics::Command_encoder& encoder, const erhe::graphics::Ring_buffer_range& range);

private:
    Light_interface&                         m_light_interface;
    erhe::graphics::Ring_buffer_client       m_light_buffer;
    erhe::graphics::Ring_buffer_client       m_control_buffer;
    std::shared_ptr<erhe::graphics::Texture> m_fallback_shadow_texture;
};

} // namespace erhe::scene_renderer
