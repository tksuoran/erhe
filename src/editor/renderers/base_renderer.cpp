#include "renderers/base_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "log.hpp"

#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/span.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/tracy_client.hpp"
#include "erhe/ui/font.hpp"
#include "erhe/ui/rectangle.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"

namespace editor
{

using namespace erhe::toolkit;
using namespace erhe::graphics;
using namespace erhe::primitive;
using namespace erhe::scene;
using namespace erhe;
using namespace gl;
using namespace glm;
using namespace std;

void Base_renderer::base_connect(erhe::components::Component* component)
{
    m_programs = component->get<Programs>();
}

void Base_renderer::create_frame_resources(size_t material_count,
                                           size_t light_count,
                                           size_t camera_count,
                                           size_t primitive_count,
                                           size_t draw_count)
{
    ZoneScoped;

    for (size_t i = 0; i < s_frame_resources_count; ++i)
    {
        m_frame_resources.emplace_back(m_programs->material_block .size_bytes(),   material_count,
                                       m_programs->light_block    .size_bytes(),   light_count,
                                       m_programs->camera_block   .size_bytes(),   camera_count,
                                       m_programs->primitive_block.size_bytes(),   primitive_count,
                                       sizeof(gl::Draw_elements_indirect_command), draw_count);
    }
}


auto Base_renderer::current_frame_resources() -> Frame_resources&
{
    return m_frame_resources[m_current_frame_resource_slot];
}

void Base_renderer::next_frame()
{
    m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % s_frame_resources_count;

    m_material_writer.reset();
    m_light_writer.reset();
    m_camera_writer.reset();
    m_primitive_writer.reset();
    m_draw_indirect_writer.reset();
}

void Base_renderer::reset_id_ranges()
{
    m_id_offset = 0;
    m_id_ranges.clear();
}

auto Base_renderer::update_primitive_buffer(const Mesh_collection& meshes,
                                            uint64_t               visibility_mask)
-> Base_renderer::Buffer_range
{
    ZoneScoped;

    log_render.trace("{}(meshes.size() = {})\n", __func__, meshes.size());

    m_primitive_writer.begin();
    size_t      entry_size         = m_programs->primitive_struct.size_bytes();
    auto        primitive_gpu_data = current_frame_resources().primitive_buffer.map();
    size_t      primitive_index    = 0;
    const auto& offsets            = m_programs->primitive_block_offsets;
    for (auto mesh : meshes)
    {
        if ((mesh->visibility_mask & visibility_mask) == 0)
        {
            continue;
        }
        auto node = mesh->node();
        VERIFY(node);
        node->update(); // TODO cache

        size_t mesh_primitive_index{0};
        for (auto& primitive : mesh->primitives)
        {
            auto* primitive_geometry = primitive.primitive_geometry.get();
            log_render.trace("primitive_index = {}\n", primitive_index);

            uint32_t count        = static_cast<uint32_t>(primitive_geometry->fill_indices.index_count);
            uint32_t power_of_two = next_power_of_two(count);
            uint32_t mask         = power_of_two - 1;
            uint32_t current_bits = m_id_offset & mask;
            if (current_bits != 0)
            {
                uint add = power_of_two - current_bits;
                m_id_offset += add;
            }

            vec3 id_offset_vec3 = vec3_from_uint(m_id_offset);
            vec3 id_offset_vec4 = vec4(id_offset_vec3, 0.0f);

            mat4     world_from_node = node->world_from_node();
            uint32_t material_index  = (primitive.material != nullptr) ? static_cast<uint32_t>(primitive.material->index) : 0u;
            uint32_t extra2          = 0;
            uint32_t extra3          = 0;
            const auto color_span = (primitive_color_source == Primitive_color_source::id_offset           ) ? as_span(id_offset_vec4          ) :
                                    (primitive_color_source == Primitive_color_source::mesh_wireframe_color) ? as_span(mesh->wireframe_color   ) :
                                                                                                               as_span(primitive_constant_color);
            const auto size_span = (primitive_size_source == Primitive_size_source::mesh_point_size) ? as_span(mesh->point_size       ) :
                                   (primitive_size_source == Primitive_size_source::mesh_line_width) ? as_span(mesh->line_width       ) :
                                                                                                       as_span(primitive_constant_size);
            //memset(reinterpret_cast<uint8_t*>(model_gpu_data.data()) + offset, 0, entry_size);
            write(primitive_gpu_data, m_primitive_writer.write_offset + offsets.world_from_node, as_span(world_from_node));
            write(primitive_gpu_data, m_primitive_writer.write_offset + offsets.color,           color_span              );
            write(primitive_gpu_data, m_primitive_writer.write_offset + offsets.material_index,  as_span(material_index ));
            write(primitive_gpu_data, m_primitive_writer.write_offset + offsets.size,            size_span               );
            write(primitive_gpu_data, m_primitive_writer.write_offset + offsets.extra2,          as_span(extra2         ));
            write(primitive_gpu_data, m_primitive_writer.write_offset + offsets.extra3,          as_span(extra3         ));
            m_primitive_writer.write_offset += entry_size;

            Id_range r;
            r.offset          = m_id_offset;
            r.length          = count;
            r.mesh            = mesh;
            r.primitive_index = mesh_primitive_index++;
            m_id_ranges.push_back(r);

            m_id_offset += count;

            ++primitive_index;
        }
    }
    m_primitive_writer.end();

    return m_primitive_writer.range;
}

auto Base_renderer::update_light_buffer(Light_collection& lights,
                                        Viewport          light_texture_viewport,
                                        glm::vec4         ambient_light)
-> Base_renderer::Buffer_range
{
    ZoneScoped;

    log_render.trace("{}(lights.size() = {})\n", __func__, lights.size());

    size_t      entry_size     = m_programs->light_struct.size_bytes();
    auto        light_gpu_data = current_frame_resources().light_buffer.map();
    int         light_index    = 0;
    const auto& offsets        = m_programs->light_block_offsets;
    uint32_t    directional_light_count{0};
    uint32_t    spot_light_count{0};
    uint32_t    point_light_count{0};

    m_light_writer.begin();

    m_light_writer.write_offset += offsets.light_struct;

    for (auto light : lights)
    {
        log_render.trace("light_index = {}\n", light_index);
        switch (light->type)
        {
            case Light::Type::directional: ++directional_light_count; break;
            case Light::Type::point:       ++point_light_count; break;
            case Light::Type::spot:        ++spot_light_count; break;
            default: break;
        }

        light->update(light_texture_viewport);

        auto node = light->node();
        VERIFY(node);

        glm::vec4 position  = node->world_from_node() * vec4(0.0f, 0.0f, 0.0f, 1.0f);
        glm::vec4 direction = node->world_from_node() * vec4(0.0f, 0.0f, 1.0f, 0.0f);
        glm::vec4 radiance  = vec4(light->intensity * light->color, light->intensity);
        direction = normalize(direction);
        float inner_spot_cos = std::cos(light->inner_spot_angle * 0.5f);
        float outer_spot_cos = std::cos(light->outer_spot_angle * 0.5f);
        position .w = inner_spot_cos;
        direction.w = outer_spot_cos;
        radiance .w = light->range;
        write(light_gpu_data, m_light_writer.write_offset + offsets.light.texture_from_world,           as_span(light->texture_from_world()));
        write(light_gpu_data, m_light_writer.write_offset + offsets.light.position_and_inner_spot_cos,  as_span(position));
        write(light_gpu_data, m_light_writer.write_offset + offsets.light.direction_and_outer_spot_cos, as_span(direction));
        write(light_gpu_data, m_light_writer.write_offset + offsets.light.radiance_and_range,           as_span(radiance));
        m_light_writer.write_offset += entry_size;
        ++light_index;
    }
    write(light_gpu_data, m_light_writer.range.first_byte_offset + offsets.directional_light_count, as_span(directional_light_count));
    write(light_gpu_data, m_light_writer.range.first_byte_offset + offsets.point_light_count,       as_span(point_light_count)      );
    write(light_gpu_data, m_light_writer.range.first_byte_offset + offsets.spot_light_count,        as_span(spot_light_count)       );
    write(light_gpu_data, m_light_writer.range.first_byte_offset + offsets.ambient_light,           as_span(ambient_light)          );

    m_light_writer.end();

    return m_light_writer.range;
}

auto Base_renderer::update_material_buffer(const Material_collection& materials) -> Base_renderer::Buffer_range
{
    ZoneScoped;

    log_render.trace("{}(materials.size() = {})\n", __func__, materials.size());

    size_t      entry_size        = m_programs->material_struct.size_bytes();
    auto        material_gpu_data = current_frame_resources().material_buffer.map();
    size_t      material_index    = 0;
    const auto& offsets           = m_programs->material_block_offsets;
    m_material_writer.begin();
    for (auto material : materials)
    {
        log_render.trace("material_index = {}\n", material_index);
        memset(reinterpret_cast<uint8_t*>(material_gpu_data.data()) + m_material_writer.write_offset, 0, entry_size);
        write(material_gpu_data, m_material_writer.write_offset + offsets.metallic    , as_span(material->metallic    ));
        write(material_gpu_data, m_material_writer.write_offset + offsets.roughness   , as_span(material->roughness   ));
        write(material_gpu_data, m_material_writer.write_offset + offsets.anisotropy  , as_span(material->anisotropy  ));
        write(material_gpu_data, m_material_writer.write_offset + offsets.transparency, as_span(material->transparency));
        write(material_gpu_data, m_material_writer.write_offset + offsets.base_color  , as_span(material->base_color  ));
        write(material_gpu_data, m_material_writer.write_offset + offsets.emissive    , as_span(material->emissive    ));
        m_material_writer.write_offset += entry_size;
        ++material_index;
    }
    m_material_writer.end();

    return m_material_writer.range;
}

auto Base_renderer::update_camera_buffer(ICamera& camera,
                                         Viewport viewport)
-> Base_renderer::Buffer_range
{
    ZoneScoped;

    log_render.trace("{}\n", __func__);

    camera.update(viewport);

    auto camera_gpu_data = current_frame_resources().camera_buffer.map();
    mat4 world_from_node = camera.node()->world_from_node();
    mat4 world_from_clip = camera.world_from_clip();
    mat4 clip_from_world = camera.clip_from_world();

    float exposure = 1.0f;
    const auto& offsets = m_programs->camera_block_offsets;
    m_camera_writer.begin();
    float viewport_floats[4];
    viewport_floats[0] = static_cast<float>(viewport.x);
    viewport_floats[1] = static_cast<float>(viewport.y);
    viewport_floats[2] = static_cast<float>(viewport.width);
    viewport_floats[3] = static_cast<float>(viewport.height);
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.world_from_node, as_span(world_from_node));
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.world_from_clip, as_span(world_from_clip));
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.clip_from_world, as_span(clip_from_world));
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.viewport,        as_span(viewport_floats));
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.exposure,        as_span(exposure)       );
    m_camera_writer.write_offset += m_programs->camera_block.size_bytes();
    m_camera_writer.end();

    return m_camera_writer.range;
}


auto Base_renderer::update_draw_indirect_buffer(const Mesh_collection& meshes,
                                                Primitive_mode         primitive_mode,
                                                uint64_t               visibility_mask)
-> Base_renderer::Draw_indirect_buffer_range
{
    ZoneScoped;

    auto     draw_indirect_gpu_data = current_frame_resources().draw_indirect_buffer.map();
    uint32_t instance_count     {1};
    uint32_t base_instance      {0};
    size_t   draw_indirect_count{0};
    m_draw_indirect_writer.begin();
    for (auto mesh : meshes)
    {
        if ((mesh->visibility_mask & visibility_mask) == 0)
        {
            continue;
        }
        for (auto& primitive : mesh->primitives)
        {
            auto* primitive_geometry = primitive.primitive_geometry.get();
            auto index_range = primitive_geometry->index_range(primitive_mode);
            if (index_range.index_count == 0)
            {
                continue;
            }

            uint32_t index_count = static_cast<uint32_t>(index_range.index_count);
            if (m_max_index_count_enable)
            {
                index_count = std::min(index_count, static_cast<uint32_t>(m_max_index_count));
            }

            uint32_t base_index  = primitive_geometry->base_index();
            uint32_t first_index = static_cast<uint32_t>(index_range.first_index + base_index);
            uint32_t base_vertex = primitive_geometry->base_vertex();

            auto draw_command = gl::Draw_elements_indirect_command{index_count,
                                                                   instance_count,
                                                                   first_index,
                                                                   base_vertex,
                                                                   base_instance};

            erhe::graphics::write(draw_indirect_gpu_data,
                                  m_draw_indirect_writer.write_offset,
                                  erhe::graphics::as_span(draw_command));

            m_draw_indirect_writer.write_offset += sizeof(gl::Draw_elements_indirect_command);
            ++draw_indirect_count;
        }
    }

    m_draw_indirect_writer.end();

    return { m_draw_indirect_writer.range, draw_indirect_count};
}

void Base_renderer::bind_material_buffer()
{
    if (m_material_writer.range.byte_count == 0)
    {
        return;
    }

    auto& material_buffer = current_frame_resources().material_buffer;
    gl::bind_buffer_range(material_buffer.target(),
                          static_cast<GLuint>    (programs()->material_block.binding_point()),
                          static_cast<GLuint>    (material_buffer.gl_name()),
                          static_cast<GLintptr>  (m_material_writer.range.first_byte_offset),
                          static_cast<GLsizeiptr>(m_material_writer.range.byte_count));
}

void Base_renderer::bind_light_buffer()
{
    if (m_light_writer.range.byte_count == 0)
    {
        return;
    }

    auto& buffer = current_frame_resources().light_buffer;
    gl::bind_buffer_range(buffer.target(),
                          static_cast<GLuint>    (programs()->light_block.binding_point()),
                          static_cast<GLuint>    (buffer.gl_name()),
                          static_cast<GLintptr>  (m_light_writer.range.first_byte_offset),
                          static_cast<GLsizeiptr>(m_light_writer.range.byte_count));
}

void Base_renderer::bind_camera_buffer()
{
    if (m_camera_writer.range.byte_count == 0)
    {
        return;
    }
    auto& buffer = current_frame_resources().camera_buffer;
    gl::bind_buffer_range(buffer.target(),
                          static_cast<GLuint>    (programs()->camera_block.binding_point()),
                          static_cast<GLuint>    (buffer.gl_name()),
                          static_cast<GLintptr>  (m_camera_writer.range.first_byte_offset),
                          static_cast<GLsizeiptr>(m_camera_writer.range.byte_count));
}

void Base_renderer::bind_primitive_buffer()
{
    if (m_primitive_writer.range.byte_count == 0)
    {
        return;
    }

    auto& buffer = current_frame_resources().primitive_buffer;
    gl::bind_buffer_range(buffer.target(),
                          static_cast<GLuint>    (programs()->primitive_block.binding_point()),
                          static_cast<GLuint>    (buffer.gl_name()),
                          static_cast<GLintptr>  (m_primitive_writer.range.first_byte_offset),
                          static_cast<GLsizeiptr>(m_primitive_writer.range.byte_count));
}

void Base_renderer::bind_draw_indirect_buffer()
{
    if (m_draw_indirect_writer.range.byte_count == 0)
    {
        return;
    }

    auto& buffer = current_frame_resources().draw_indirect_buffer;

    gl::bind_buffer(buffer.target(),
                    static_cast<GLuint>(buffer.gl_name()));
}

void Base_renderer::debug_properties_window()
{
    ImGui::Begin("Base Renderer Debug Properties");
    ImGui::Checkbox("Enable Max Index Count", &m_max_index_count_enable);
    ImGui::SliderInt("Max Index Count", &m_max_index_count, 0, 256);
    ImGui::End();
}

} // namespace editor
