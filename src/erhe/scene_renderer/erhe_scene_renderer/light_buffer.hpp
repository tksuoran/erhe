#pragma once

#include "erhe_renderer/gpu_ring_buffer.hpp"

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_math/viewport.hpp"

#include <memory>

namespace erhe::graphics {
    class Texture;
}
namespace erhe::primitive {
    class Material;
}

namespace erhe::scene_renderer {

class Light_struct
{
public:
    std::size_t clip_from_world;                // mat4
    std::size_t texture_from_world;             // mat4
    std::size_t position_and_inner_spot_cos;    // vec4 (vec3, float)
    std::size_t direction_and_outer_spot_cos;   // vec4 (vec3, float)
    std::size_t radiance_and_range;             // vec4 (float, float, padding, padding )
};

class Light_block
{
public:
    std::size_t  shadow_texture_compare;    // uvec2
    std::size_t  shadow_texture_no_compare; // uvec2

    std::size_t  shadow_bias_scale;         // float
    std::size_t  shadow_min_bias;           // float
    std::size_t  shadow_max_bias;           // float
    std::size_t  reserved_0;                // float

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

class Light_interface
{
public:
    explicit Light_interface(erhe::graphics::Instance& graphics_instance);

    std::size_t                     max_light_count;
    erhe::graphics::Shader_resource light_block;
    erhe::graphics::Shader_resource light_control_block;
    erhe::graphics::Shader_resource light_struct;
    Light_block                     offsets;
    std::size_t                     light_index_offset;
};

// Selects camera for which the shadow frustums are fitted
class Light_projections
{
public:
    Light_projections();
    Light_projections(
        const std::span<const std::shared_ptr<erhe::scene::Light>>& lights,
        const erhe::scene::Camera*                                  view_camera,
        ////const erhe::math::Viewport&                             view_camera_viewport,
        const erhe::math::Viewport&                                 light_texture_viewport,
        const std::shared_ptr<erhe::graphics::Texture>&             shadow_map_texture,
        uint64_t                                                    shadow_map_texture_handle_compare,
        uint64_t                                                    shadow_map_texture_handle_no_compare
    );

    // Warning: Returns pointer to element of member vector. That pointer
    //          should remain stable as long as Light_projections stays
    //          alive.
    [[nodiscard]] auto get_light_projection_transforms_for_light(const erhe::scene::Light* light) -> erhe::scene::Light_projection_transforms*;
    [[nodiscard]] auto get_light_projection_transforms_for_light(const erhe::scene::Light* light) const -> const erhe::scene::Light_projection_transforms*;

    erhe::scene::Light_projection_parameters              parameters;
    std::vector<erhe::scene::Light_projection_transforms> light_projection_transforms;
    std::shared_ptr<erhe::graphics::Texture>              shadow_map_texture;
    uint64_t                                              shadow_map_texture_handle_compare;
    uint64_t                                              shadow_map_texture_handle_no_compare;

    // TODO A bit hacky injection of these parameters..
    float                                                 brdf_phi         {0.0f};
    float                                                 brdf_incident_phi{0.0f};
    std::shared_ptr<erhe::primitive::Material>            brdf_material    {};

    static float                                          s_shadow_bias_scale;
    static float                                          s_shadow_min_bias;
    static float                                          s_shadow_max_bias;
};

class Light_buffer
{
public:
    Light_buffer(erhe::graphics::Instance& graphics_instance, Light_interface& light_interface);

    auto update(
        const std::span<const std::shared_ptr<erhe::scene::Light>>& lights,
        const Light_projections*                                    light_projections,
        const glm::vec3&                                            ambient_light
    ) -> erhe::renderer::Buffer_range;

    auto update_control(std::size_t light_index) -> erhe::renderer::Buffer_range;

    void bind_light_buffer  (const erhe::renderer::Buffer_range& range);
    void bind_control_buffer(const erhe::renderer::Buffer_range& range);

private:
    Light_interface&                m_light_interface;
    erhe::renderer::GPU_ring_buffer m_light_buffer;
    erhe::renderer::GPU_ring_buffer m_control_buffer;
};

} // namespace erhe::scene_renderer
