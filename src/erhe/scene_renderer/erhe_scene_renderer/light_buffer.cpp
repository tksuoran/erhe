// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_renderer/renderer_config.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/transform.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

[[nodiscard]] auto get_max_light_count() -> std::size_t
{
    int max_light_count{1};
    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "renderer");
    ini.get("max_light_count", max_light_count);
    return max_light_count;
}

Light_interface::Light_interface(erhe::graphics::Instance& graphics_instance)
    : max_light_count{get_max_light_count()}
    , light_block        {graphics_instance, "light_block",         1, erhe::graphics::Shader_resource::Type::uniform_block}
    , light_control_block{graphics_instance, "light_control_block", 2, erhe::graphics::Shader_resource::Type::uniform_block}
    , light_struct       {graphics_instance, "Light"}
    , offsets     {
        .shadow_texture          = light_block.add_uvec2("shadow_texture"         )->offset_in_parent(),
        .brdf_phi_incident_phi   = light_block.add_vec2 ("brdf_phi_incident_phi"  )->offset_in_parent(),
        .directional_light_count = light_block.add_uint ("directional_light_count")->offset_in_parent(),
        .spot_light_count        = light_block.add_uint ("spot_light_count"       )->offset_in_parent(),
        .point_light_count       = light_block.add_uint ("point_light_count"      )->offset_in_parent(),
        .brdf_material           = light_block.add_uint ("brdf_material"          )->offset_in_parent(),
        .ambient_light           = light_block.add_vec4 ("ambient_light"          )->offset_in_parent(),
        .reserved_2              = light_block.add_uvec4("reserved_2"             )->offset_in_parent(),
        .light = {
            .clip_from_world              = light_struct.add_mat4("clip_from_world"             )->offset_in_parent(),
            .texture_from_world           = light_struct.add_mat4("texture_from_world"          )->offset_in_parent(),
            .position_and_inner_spot_cos  = light_struct.add_vec4("position_and_inner_spot_cos" )->offset_in_parent(),
            .direction_and_outer_spot_cos = light_struct.add_vec4("direction_and_outer_spot_cos")->offset_in_parent(),
            .radiance_and_range           = light_struct.add_vec4("radiance_and_range"          )->offset_in_parent(),
        },
        .light_struct = light_block.add_struct("lights", &light_struct, max_light_count)->offset_in_parent()
    }
    , light_index_offset{
        light_control_block.add_uint("light_index")->offset_in_parent()
    }
{
}

Light_buffer::Light_buffer(erhe::graphics::Instance& graphics_instance, Light_interface& light_interface)
    : m_light_interface{light_interface}
    , m_light_buffer{
        graphics_instance,
        erhe::renderer::GPU_ring_buffer_create_info{
            .target        = gl::Buffer_target::uniform_buffer,
            .binding_point = light_interface.light_block.binding_point(),
            .size          = 25 * (light_interface.offsets.light_struct + light_interface.max_light_count * light_interface.light_struct.size_bytes()),
            .debug_label   = "light"
        }
    }
    , m_control_buffer{
        graphics_instance,
        erhe::renderer::GPU_ring_buffer_create_info{
            .target        = gl::Buffer_target::uniform_buffer,
            .binding_point = light_interface.light_control_block.binding_point(),
            .size          = 32 * (m_light_interface.light_control_block.size_bytes()),
            .debug_label   = "light_control"
        }
    }
{
}

Light_projections::Light_projections()
    : shadow_map_texture_handle{erhe::graphics::invalid_texture_handle}
{
}

Light_projections::Light_projections(
    const std::span<const std::shared_ptr<erhe::scene::Light>>& lights,
    const erhe::scene::Camera*                                  view_camera,
    const erhe::math::Viewport&                                 light_texture_viewport,
    const std::shared_ptr<erhe::graphics::Texture>&             shadow_map_texture,
    uint64_t                                                    shadow_map_texture_handle
)
    : parameters{
        .view_camera         = view_camera,
        .shadow_map_viewport = light_texture_viewport
    }
    , shadow_map_texture       {shadow_map_texture}
    , shadow_map_texture_handle{shadow_map_texture_handle}
{
    light_projection_transforms.clear();

    for (const auto& light : lights) {
        const std::size_t light_index = light_projection_transforms.size();
        //log_render->info("update light projection for {} index {}", light->describe(), light_index);
        auto transforms = light->projection_transforms(parameters);
        transforms.index = light_index;
        light_projection_transforms.push_back(transforms);
    }

    SPDLOG_LOGGER_TRACE(
        log_render,
        "projection_transforms.size() = {}, texture_from_world,size = {}",
        projection_transforms.size(),
        texture_from_world.size()
    );
}

auto Light_projections::get_light_projection_transforms_for_light(const erhe::scene::Light* light) -> erhe::scene::Light_projection_transforms*
{
    for (auto& i : light_projection_transforms) {
        if (i.light == light) {
            return &i;
        }
    }
    return nullptr;
}

auto Light_projections::get_light_projection_transforms_for_light(const erhe::scene::Light* light) const -> const erhe::scene::Light_projection_transforms*
{
    for (auto& i : light_projection_transforms) {
        if (i.light == light) {
            return &i;
        }
    }
    return nullptr;
}

auto Light_buffer::update(
    const std::span<const std::shared_ptr<erhe::scene::Light>>& lights,
    const Light_projections*                                    light_projections,
    const glm::vec3&                                            ambient_light
) -> erhe::renderer::Buffer_range
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(log_render, "lights.size() = {}, m_light_buffer.writer().write_offset = {}", lights.size(), m_light_buffer.writer().write_offset);

    const auto                   light_struct_size = m_light_interface.light_struct.size_bytes();
    const auto&                  offsets           = m_light_interface.offsets;

    // NOTE: As long as we are using uniform buffer, always fill in data max lights
    // (m_light_interface.max_light_count) instea of lights.size(), and pad the
    // unused part of the array with zeros.
    const size_t                 exact_byte_count  = offsets.light_struct + m_light_interface.max_light_count * light_struct_size;
    erhe::renderer::Buffer_range buffer_range      = m_light_buffer.open(erhe::renderer::Ring_buffer_usage::CPU_write, exact_byte_count);
    std::span<std::byte>         light_gpu_data    = buffer_range.get_span();
    ERHE_VERIFY(light_gpu_data.size_bytes() >= exact_byte_count);
    uint32_t       directional_light_count{0u};
    uint32_t       spot_light_count       {0u};
    uint32_t       point_light_count      {0u};
    const uint32_t uvec4_zero[4]          {0u, 0u, 0u, 0u};
    const uint32_t shadow_map_texture_handle_uvec2[2] = {
        light_projections ? static_cast<uint32_t>((light_projections->shadow_map_texture_handle & 0xffffffffu)) : 0xffffffffu,
        light_projections ? static_cast<uint32_t>( light_projections->shadow_map_texture_handle >> 32u) : 0
    };

    using erhe::graphics::as_span;
    using erhe::graphics::write;

    std::size_t write_offset = 0;
    const std::size_t common_offset = write_offset;

    write_offset += offsets.light_struct;
    const std::size_t light_array_offset = write_offset;
    std::size_t max_light_index{0};

    for (const auto& light : lights) {
        ERHE_VERIFY(light);
        erhe::scene::Node* node = light->get_node();
        ERHE_VERIFY(node != nullptr);

        switch (light->type) {
            case erhe::scene::Light_type::directional: ++directional_light_count; break;
            case erhe::scene::Light_type::point:       ++point_light_count; break;
            case erhe::scene::Light_type::spot:        ++spot_light_count; break;
            default: break;
        }

        using vec2 = glm::vec2;
        using vec3 = glm::vec3;
        using vec4 = glm::vec4;
        using mat4 = glm::mat4;
        auto* light_projection_transforms = (light_projections != nullptr)
            ? light_projections->get_light_projection_transforms_for_light(light.get())
            : nullptr;
        if (light_projection_transforms == nullptr) {
            continue;
        }

        const mat4 texture_from_world   = light_projection_transforms->texture_from_world.get_matrix();
        const vec3 direction            = vec3{node->world_from_node() * vec4{0.0f, 0.0f, 1.0f, 0.0f}};
        const vec3 position             = vec3{light_projection_transforms->world_from_light_camera.get_matrix() * vec4{0.0f, 0.0f, 0.0f, 1.0f}};
        const vec4 radiance             = vec4{light->intensity * light->color, light->range};
        const auto inner_spot_cos       = std::cos(light->inner_spot_angle * 0.5f);
        const auto outer_spot_cos       = std::cos(light->outer_spot_angle * 0.5f);
        const vec4 position_inner_spot  = vec4{position, inner_spot_cos};
        const vec4 direction_outer_spot = vec4{glm::normalize(vec3{direction}), outer_spot_cos};
        const auto light_index          = light_projection_transforms->index;
        const auto light_offset         = light_array_offset + light_index * light_struct_size;

        ERHE_VERIFY(light_offset < exact_byte_count);
        max_light_index = std::max(max_light_index, light_index);
        write(light_gpu_data, light_offset + offsets.light.clip_from_world,              as_span(light_projection_transforms->clip_from_world.get_matrix()));
        write(light_gpu_data, light_offset + offsets.light.texture_from_world,           as_span(texture_from_world));
        write(light_gpu_data, light_offset + offsets.light.position_and_inner_spot_cos,  as_span(position_inner_spot));
        write(light_gpu_data, light_offset + offsets.light.direction_and_outer_spot_cos, as_span(direction_outer_spot));
        write(light_gpu_data, light_offset + offsets.light.radiance_and_range,           as_span(radiance));
    }
    // Fill in unused part of the array
    size_t padding_light_offset = (max_light_index + 1);
    size_t padding_light_count = m_light_interface.max_light_count - padding_light_offset;
    memset(
        light_gpu_data.data() + light_array_offset + padding_light_offset * light_struct_size,
        0,
        padding_light_count * light_struct_size
    );
    write_offset += m_light_interface.max_light_count * light_struct_size;

    const auto brdf_phi_incident_phi = light_projections != nullptr ? glm::vec2{light_projections->brdf_phi, light_projections->brdf_incident_phi} : glm::vec2{0.0f, 0.0f};
    const auto brdf_material         = light_projections != nullptr ? (light_projections->brdf_material ? light_projections->brdf_material->material_buffer_index : 0) : 0;

    // Late write to begin of buffer to full in light counts
    write(light_gpu_data, common_offset + offsets.shadow_texture,          as_span(shadow_map_texture_handle_uvec2));
    write(light_gpu_data, common_offset + offsets.brdf_phi_incident_phi,   as_span(brdf_phi_incident_phi)    );
    write(light_gpu_data, common_offset + offsets.directional_light_count, as_span(directional_light_count)  );
    write(light_gpu_data, common_offset + offsets.spot_light_count,        as_span(spot_light_count)         );
    write(light_gpu_data, common_offset + offsets.point_light_count,       as_span(point_light_count)        );
    write(light_gpu_data, common_offset + offsets.brdf_material,           as_span(brdf_material)            );
    write(light_gpu_data, common_offset + offsets.ambient_light,           as_span(ambient_light)            );
    write(light_gpu_data, common_offset + offsets.reserved_2,              as_span(uvec4_zero)               );

    buffer_range.close(write_offset);
    SPDLOG_LOGGER_TRACE(log_draw, "wrote up to {} entries to light buffer", padding_light_offset);

    return buffer_range;
}

auto Light_buffer::update_control(const std::size_t light_index) -> erhe::renderer::Buffer_range
{
    ERHE_PROFILE_FUNCTION();

    const auto                   entry_size   = m_light_interface.light_control_block.size_bytes();
    erhe::renderer::Buffer_range buffer_range = m_control_buffer.open(erhe::renderer::Ring_buffer_usage::CPU_write, entry_size);
    const auto                   gpu_data     = buffer_range.get_span();
    size_t                       write_offset = 0;

    using erhe::graphics::as_span;
    using erhe::graphics::write;

    const auto uint_light_index = static_cast<uint32_t>(light_index);
    write(gpu_data, write_offset + 0, as_span(uint_light_index));
    write_offset += entry_size;

    buffer_range.close(write_offset);

    return buffer_range;
}

void Light_buffer::bind_light_buffer(const erhe::renderer::Buffer_range& range)
{
    m_light_buffer.bind(range);
}

void Light_buffer::bind_control_buffer(const erhe::renderer::Buffer_range& range)
{
    m_control_buffer.bind(range);
}

} // namespace erhe::scene_renderer
