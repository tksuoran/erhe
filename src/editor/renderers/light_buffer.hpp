#pragma once

#include "renderers/multi_buffer.hpp"

#include "erhe/graphics/shader_resource.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/transform.hpp"
#include "erhe/scene/viewport.hpp"

#include <memory>
#include <optional>

namespace editor
{

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
    std::size_t  shadow_texture;          // uvec2
    std::size_t  reserved_1;              // uvec2
    std::size_t  directional_light_count; // uint
    std::size_t  spot_light_count;        // uint
    std::size_t  point_light_count;       // uint
    std::size_t  reserved_0;              // uint
    std::size_t  ambient_light;           // vec4
    std::size_t  reserved_2;              // uvec3
    Light_struct light;
    std::size_t  light_struct;
};

class Light_interface
{
public:
    explicit Light_interface(std::size_t max_light_count);

    erhe::graphics::Shader_resource light_block;
    erhe::graphics::Shader_resource light_control_block{"light_control_block", 1, erhe::graphics::Shader_resource::Type::uniform_block};
    erhe::graphics::Shader_resource light_struct       {"Light"};
    Light_block                     offsets            {};
    std::size_t                     light_index_offset {};
    std::size_t                     max_light_count;
};

// Selects camera for which the shadow frustums are fitted
class Light_projections
{
public:
    Light_projections();
    Light_projections(
        const gsl::span<const std::shared_ptr<erhe::scene::Light>>& lights,
        const erhe::scene::Camera*                                  view_camera,
        const erhe::scene::Viewport&                                view_camera_viewport,
        const erhe::scene::Viewport&                                light_texture_viewport,
        uint64_t                                                    shadow_map_texture_handle
    );

    // Warning: Returns pointer to element of member vector. That pointer
    //          should remain stable as long as Light_projections stays
    //          alive.
    [[nodiscard]] auto get_light_projection_transforms_for_light(
        const erhe::scene::Light* light
    ) -> erhe::scene::Light_projection_transforms*;
    [[nodiscard]] auto get_light_projection_transforms_for_light(
        const erhe::scene::Light* light
    ) const -> const erhe::scene::Light_projection_transforms*;

    erhe::scene::Light_projection_parameters              parameters;
    std::vector<erhe::scene::Light_projection_transforms> light_projection_transforms;
    uint64_t                                              shadow_map_texture_handle;
};

class Light_buffer
{
public:
    explicit Light_buffer(const Light_interface& light_interface);

    auto update(
        const gsl::span<const std::shared_ptr<erhe::scene::Light>>& lights,
        const Light_projections&                                    light_projections,
        const glm::vec3&                                            ambient_light
    ) -> erhe::application::Buffer_range;

    auto update_control(
        std::size_t light_index
    ) -> erhe::application::Buffer_range;

    void next_frame         ();
    void bind_light_buffer  ();
    void bind_control_buffer();

private:
    const Light_interface& m_light_interface;

    Multi_buffer           m_light_buffer;
    Multi_buffer           m_control_buffer;
};

} // namespace editor
