// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "renderers/light_buffer.hpp"
#include "renderers/program_interface.hpp"
#include "editor_log.hpp"

#include "erhe/log/log_glm.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/projection.hpp"
#include "erhe/scene/transform.hpp"
#include "erhe/toolkit/profile.hpp"

namespace editor
{

Light_interface::Light_interface(std::size_t max_light_count)
    : light_block{
        "light_block",
        1,
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , light_control_block{
        "light_control_block",
        2,
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , light_struct{"Light"}
    , offsets     {
        .shadow_texture          = light_block.add_uvec2("shadow_texture"         )->offset_in_parent(),
        .reserved_1              = light_block.add_uvec2("reserved_1"             )->offset_in_parent(),
        .directional_light_count = light_block.add_uint ("directional_light_count")->offset_in_parent(),
        .spot_light_count        = light_block.add_uint ("spot_light_count"       )->offset_in_parent(),
        .point_light_count       = light_block.add_uint ("point_light_count"      )->offset_in_parent(),
        .reserved_0              = light_block.add_uint ("reserved_0"             )->offset_in_parent(),
        .ambient_light           = light_block.add_vec4 ("ambient_light"          )->offset_in_parent(),
        .reserved_2              = light_block.add_uvec4("reserved_2"             )->offset_in_parent(),
        .light = {
            .clip_from_world              = light_struct.add_mat4("clip_from_world"             )->offset_in_parent(),
            .texture_from_world           = light_struct.add_mat4("texture_from_world"          )->offset_in_parent(),
            .position_and_inner_spot_cos  = light_struct.add_vec4("position_and_inner_spot_cos" )->offset_in_parent(),
            .direction_and_outer_spot_cos = light_struct.add_vec4("direction_and_outer_spot_cos")->offset_in_parent(),
            .radiance_and_range           = light_struct.add_vec4("radiance_and_range"          )->offset_in_parent(),
        },
        .light_struct            = light_block.add_struct("lights", &light_struct, max_light_count)->offset_in_parent()
    }
    , light_index_offset{
        light_control_block.add_uint("light_index")->offset_in_parent()
    }
    , max_light_count{max_light_count}
{
}

Light_buffer::Light_buffer(const Light_interface& light_interface)
    : m_light_interface{light_interface}
    , m_light_buffer   {"light"}
    , m_control_buffer {"light_control"}
{
    m_light_buffer.allocate(
        gl::Buffer_target::uniform_buffer,
        m_light_interface.light_block.binding_point(),
        m_light_interface.light_block.size_bytes()
    );

    m_control_buffer.allocate(
        gl::Buffer_target::uniform_buffer,
        m_light_interface.light_control_block.binding_point(),
        m_light_interface.light_control_block.size_bytes() * 40
    );
}

Light_projections::Light_projections()
{
}

Light_projections::Light_projections(
    const gsl::span<const std::shared_ptr<erhe::scene::Light>>& lights,
    const erhe::scene::Camera*                                  view_camera,
    ////const erhe::scene::Viewport&                                view_camera_viewport,
    const erhe::scene::Viewport&                                light_texture_viewport,
    uint64_t                                                    shadow_map_texture_handle
)
    : parameters
        {
            .view_camera          = view_camera,
            //.view_camera_viewport = view_camera_viewport,
            .shadow_map_viewport  = light_texture_viewport
        }
    , shadow_map_texture_handle{shadow_map_texture_handle}
{
    light_projection_transforms.clear();

    for (const auto& light : lights)
    {
        const std::size_t light_index = light_projection_transforms.size();
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

[[nodiscard]] auto Light_projections::get_light_projection_transforms_for_light(
    const erhe::scene::Light* light
) -> erhe::scene::Light_projection_transforms*
{
    for (auto& i : light_projection_transforms)
    {
        if (i.light == light)
        {
            return &i;
        }
    }
    return nullptr;
}

[[nodiscard]] auto Light_projections::get_light_projection_transforms_for_light(
    const erhe::scene::Light* light
) const -> const erhe::scene::Light_projection_transforms*
{
    for (auto& i : light_projection_transforms)
    {
        if (i.light == light)
        {
            return &i;
        }
    }
    return nullptr;
}

auto Light_buffer::update(
    const gsl::span<const std::shared_ptr<erhe::scene::Light>>& lights,
    const Light_projections*                                    light_projections,
    const glm::vec3&                                            ambient_light
) -> erhe::application::Buffer_range
{
    ERHE_PROFILE_FUNCTION

    SPDLOG_LOGGER_TRACE(
        log_render,
        "lights.size() = {}, m_light_buffer.writer().write_offset = {}",
        lights.size(),
        m_light_buffer.writer().write_offset
    );

    auto&          buffer            = m_light_buffer.current_buffer();
    auto&          writer            = m_light_buffer.writer();
    const auto     light_struct_size = m_light_interface.light_struct.size_bytes();
    const auto&    offsets           = m_light_interface.offsets;
    const auto     light_gpu_data    = buffer.map();
    uint32_t       directional_light_count{0u};
    uint32_t       spot_light_count       {0u};
    uint32_t       point_light_count      {0u};
    const uint32_t uint32_zero            {0u};
    const uint32_t uvec2_zero[2]          {0u, 0u};
    const uint32_t uvec4_zero[4]          {0u, 0u, 0u, 0u};
    const uint32_t shadow_map_texture_handle_uvec2[2] = {
        light_projections ? static_cast<uint32_t>((light_projections->shadow_map_texture_handle & 0xffffffffu)) : 0,
        light_projections ? static_cast<uint32_t>(light_projections->shadow_map_texture_handle >> 32u) : 0
    };

    using erhe::graphics::as_span;
    using erhe::graphics::write;

    writer.begin(buffer.target());

    const std::size_t common_offset = writer.write_offset;

    writer.write_offset += offsets.light_struct;
    const std::size_t light_array_offset = writer.write_offset;
    std::size_t max_light_index{0};

    for (const auto& light : lights)
    {
        //if ((writer.write_offset + entry_size) > buffer.capacity_byte_count())
        //{
        //    log_render->critical("light buffer capacity {} exceeded", buffer.capacity_byte_count());
        //    ERHE_FATAL("light buffer capacity exceeded");
        //    break;
        //}

        ERHE_VERIFY(light);

        switch (light->type)
        {
            //using enum erhe::scene::Light_type;
            case erhe::scene::Light_type::directional: ++directional_light_count; break;
            case erhe::scene::Light_type::point:       ++point_light_count; break;
            case erhe::scene::Light_type::spot:        ++spot_light_count; break;
            default: break;
        }

        //ERHE_VERIFY(camera != nullptr);
        using vec3 = glm::vec3;
        using vec4 = glm::vec4;
        using mat4 = glm::mat4;

        auto* light_projection_transforms = (light_projections != nullptr)
            ? light_projections->get_light_projection_transforms_for_light(light.get())
            : nullptr;
        if (light_projection_transforms == nullptr)
        {
            //// log_render->warn(
            ////     "light {} has no light projection transforms",
            ////     light->name()
            //// );
            continue;
        }

        const mat4 texture_from_world    = (light_projection_transforms != nullptr)
            ? light_projection_transforms->texture_from_world.matrix()
            : mat4{1.0f};
        const vec3  direction            = vec3{light->world_from_node() * vec4{0.0f, 0.0f, 1.0f, 0.0f}};
        const vec3  position             = (light_projection_transforms != nullptr)
            ? vec3{light_projection_transforms->world_from_light_camera.matrix() * vec4{0.0f, 0.0f, 0.0f, 1.0f}}
            : vec3{light->world_from_node() * vec4{0.0f, 0.0f, 0.0f, 1.0f}};

        const vec4  radiance             = vec4{light->intensity * light->color, light->range};
        const auto  inner_spot_cos       = std::cos(light->inner_spot_angle * 0.5f);
        const auto  outer_spot_cos       = std::cos(light->outer_spot_angle * 0.5f);
        const vec4  position_inner_spot  = vec4{position, inner_spot_cos};
        const vec4  direction_outer_spot = vec4{glm::normalize(vec3{direction}), outer_spot_cos};
        const auto  light_index          = light_projection_transforms->index;
        const auto  light_offset         = light_array_offset + light_index * light_struct_size;
        ERHE_VERIFY(light_offset < buffer.capacity_byte_count());
        max_light_index = std::max(max_light_index, light_index);
        //log_render->info(
        //    "light {} index = {} light_offset = {} color = {}",
        //    light->name(),
        //    light_index,
        //    light_offset,
        //    light->color
        //);

        write(light_gpu_data, light_offset + offsets.light.clip_from_world,              as_span(light_projection_transforms->clip_from_world.matrix()));
        write(light_gpu_data, light_offset + offsets.light.texture_from_world,           as_span(texture_from_world));
        write(light_gpu_data, light_offset + offsets.light.position_and_inner_spot_cos,  as_span(position_inner_spot));
        write(light_gpu_data, light_offset + offsets.light.direction_and_outer_spot_cos, as_span(direction_outer_spot));
        write(light_gpu_data, light_offset + offsets.light.radiance_and_range,           as_span(radiance));
    }
    writer.write_offset += max_light_index * light_struct_size;

    // Late write to begin of buffer to full in light counts
    write(light_gpu_data, common_offset + offsets.shadow_texture,          as_span(shadow_map_texture_handle_uvec2));
    write(light_gpu_data, common_offset + offsets.reserved_1,              as_span(uvec2_zero)               );
    write(light_gpu_data, common_offset + offsets.directional_light_count, as_span(directional_light_count)  );
    write(light_gpu_data, common_offset + offsets.spot_light_count,        as_span(spot_light_count)         );
    write(light_gpu_data, common_offset + offsets.point_light_count,       as_span(point_light_count)        );
    write(light_gpu_data, common_offset + offsets.reserved_0,              as_span(uint32_zero)              );
    write(light_gpu_data, common_offset + offsets.ambient_light,           as_span(ambient_light)            );
    write(light_gpu_data, common_offset + offsets.reserved_2,              as_span(uvec4_zero)               );

    writer.end();

    SPDLOG_LOGGER_TRACE(log_draw, "wrote up to {} entries to light buffer", max_light_index);

    return writer.range;
}

auto Light_buffer::update_control(std::size_t light_index) -> erhe::application::Buffer_range
{
    ERHE_PROFILE_FUNCTION

    auto&      buffer     = m_control_buffer.current_buffer();
    auto&      writer     = m_control_buffer.writer();
    const auto entry_size = m_light_interface.light_control_block.size_bytes();
    const auto gpu_data   = buffer.map();

    writer.begin(buffer.target());

    using erhe::graphics::as_span;
    using erhe::graphics::write;

    const auto uint_light_index = static_cast<uint32_t>(light_index);
    write(gpu_data, writer.range.first_byte_offset + 0, as_span(uint_light_index));
    writer.write_offset += entry_size;

    writer.end();

    return writer.range;
}

void Light_buffer::next_frame()
{
    m_light_buffer.next_frame();
    m_control_buffer.next_frame();
}

void Light_buffer::bind_light_buffer()
{
    m_light_buffer.bind();
}

void Light_buffer::bind_control_buffer()
{
    m_control_buffer.bind();
}

} // namespace editor
