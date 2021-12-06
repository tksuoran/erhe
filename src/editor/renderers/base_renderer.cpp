#include "renderers/base_renderer.hpp"
#include "renderers/program_interface.hpp"
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
#include "erhe/scene/scene.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/ui/font.hpp"
#include "erhe/ui/rectangle.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

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

Base_renderer::Base_renderer() = default;

Base_renderer::~Base_renderer() = default;

void Base_renderer::base_connect(const erhe::components::Component* component)
{
    m_program_interface = component->get<Program_interface>();
}

void Base_renderer::create_frame_resources(
    const size_t material_count,
    const size_t light_count,
    const size_t camera_count,
    const size_t primitive_count,
    const size_t draw_count
)
{
    ERHE_PROFILE_FUNCTION

    const auto& shader_resources = *m_program_interface->shader_resources.get();

    for (size_t i = 0; i < s_frame_resources_count; ++i)
    {
        m_frame_resources.emplace_back(
            shader_resources.material_block .size_bytes(), material_count,
            shader_resources.light_block    .size_bytes(), light_count,
            shader_resources.camera_block   .size_bytes(), camera_count,
            shader_resources.primitive_block.size_bytes(), primitive_count,
            sizeof(gl::Draw_elements_indirect_command),    draw_count
        );
    }
}


auto Base_renderer::current_frame_resources() -> Frame_resources&
{
    return m_frame_resources[m_current_frame_resource_slot];
}

void Base_renderer::next_frame()
{
    m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % s_frame_resources_count;

    m_material_writer     .reset();
    m_light_writer        .reset();
    m_camera_writer       .reset();
    m_primitive_writer    .reset();
    m_draw_indirect_writer.reset();
}

void Base_renderer::reset_id_ranges()
{
    m_id_offset = 0;
    m_id_ranges.clear();
}

auto Base_renderer::id_offset() const -> uint32_t
{
    return m_id_offset;
}

auto Base_renderer::id_ranges() const -> const std::vector<Id_range>&
{
    return m_id_ranges;
}

auto Base_renderer::update_primitive_buffer(
    const Mesh_layer&        mesh_layer,
    const Visibility_filter& visibility_filter,
    const bool               use_id_ranges
) -> Buffer_range
{
    ERHE_PROFILE_FUNCTION

    log_render.trace("{}(meshes.size() = {})\n", __func__, mesh_layer.meshes.size());

    m_primitive_writer.begin(current_frame_resources().primitive_buffer.target());
    const auto&  shader_resources   = *m_program_interface->shader_resources.get();
    const size_t entry_size         = shader_resources.primitive_struct.size_bytes();
    auto         primitive_gpu_data = current_frame_resources().primitive_buffer.map();
    const auto&  offsets            = shader_resources.primitive_block_offsets;
    size_t       primitive_index    = 0;
    for (auto mesh : mesh_layer.meshes)
    {
        VERIFY(mesh);
        if (!visibility_filter(mesh->visibility_mask()))
        {
            continue;
        }

        const auto& mesh_data = mesh->data;

        size_t mesh_primitive_index{0};
        for (auto& primitive : mesh_data.primitives)
        {
            const auto* const primitive_geometry = primitive.primitive_geometry.get();
            //log_render.trace("primitive_index = {}\n", primitive_index);

            const uint32_t count        = static_cast<uint32_t>(primitive_geometry->triangle_fill_indices.index_count);
            const uint32_t power_of_two = next_power_of_two(count);
            const uint32_t mask         = power_of_two - 1;
            const uint32_t current_bits = m_id_offset & mask;
            if (current_bits != 0)
            {
                const uint add = power_of_two - current_bits;
                m_id_offset += add;
            }

            const vec3     id_offset_vec3  = vec3_from_uint(m_id_offset);
            const vec4     id_offset_vec4  = vec4{id_offset_vec3, 0.0f};
            const mat4     world_from_node = mesh->world_from_node();
            const uint32_t material_index  = (primitive.material != nullptr) ? static_cast<uint32_t>(primitive.material->index) : 0u;
            const uint32_t extra2          = 0;
            const uint32_t extra3          = 0;
            const auto color_span =
                (primitive_color_source == Primitive_color_source::id_offset           ) ? as_span(id_offset_vec4           ) :
                (primitive_color_source == Primitive_color_source::mesh_wireframe_color) ? as_span(mesh_data.wireframe_color) :
                                                                                           as_span(primitive_constant_color);
            const auto size_span =
                (primitive_size_source == Primitive_size_source::mesh_point_size) ? as_span(mesh_data.point_size   ) :
                (primitive_size_source == Primitive_size_source::mesh_line_width) ? as_span(mesh_data.line_width   ) :
                                                                                    as_span(primitive_constant_size);
            //memset(reinterpret_cast<uint8_t*>(model_gpu_data.data()) + offset, 0, entry_size);
            {
                //ZoneScopedN("write");

                write(primitive_gpu_data, m_primitive_writer.write_offset + offsets.world_from_node, as_span(world_from_node));
                write(primitive_gpu_data, m_primitive_writer.write_offset + offsets.color,           color_span              );
                write(primitive_gpu_data, m_primitive_writer.write_offset + offsets.material_index,  as_span(material_index ));
                write(primitive_gpu_data, m_primitive_writer.write_offset + offsets.size,            size_span               );
                write(primitive_gpu_data, m_primitive_writer.write_offset + offsets.extra2,          as_span(extra2         ));
                write(primitive_gpu_data, m_primitive_writer.write_offset + offsets.extra3,          as_span(extra3         ));
            }
            m_primitive_writer.write_offset += entry_size;

            if (use_id_ranges)
            {
                Id_range r;
                r.offset          = m_id_offset;
                r.length          = count;
                r.mesh            = mesh;
                r.primitive_index = mesh_primitive_index++;
                m_id_ranges.push_back(r);

                m_id_offset += count;
            }

            ++primitive_index;
        }
    }

    m_primitive_writer.end();

    return m_primitive_writer.range;
}

auto Base_renderer::update_light_buffer(
    const Light_layer&      light_layer,
    const Viewport          light_texture_viewport
) -> Buffer_range
{
    ERHE_PROFILE_FUNCTION

    log_render.trace("{}(lights.size() = {})\n", __func__, light_layer.lights.size());

    const auto&  shader_resources = *m_program_interface->shader_resources.get();
    const size_t entry_size       = shader_resources.light_struct.size_bytes();
    const auto&  offsets          = shader_resources.light_block_offsets;
    auto         light_gpu_data   = current_frame_resources().light_buffer.map();
    int          light_index      = 0;
    uint32_t     directional_light_count{0};
    uint32_t     spot_light_count{0};
    uint32_t     point_light_count{0};

    m_light_writer.begin(current_frame_resources().light_buffer.target());

    m_light_writer.write_offset += offsets.light_struct;

    for (auto light : light_layer.lights)
    {
        VERIFY(light);

        log_render.trace("light_index = {}\n", light_index);
        switch (light->type)
        {
            case Light::Type::directional: ++directional_light_count; break;
            case Light::Type::point:       ++point_light_count; break;
            case Light::Type::spot:        ++spot_light_count; break;
            default: break;
        }

        light->update(light_texture_viewport);

        const vec3  direction            = vec3{light->world_from_node() * vec4{0.0f, 0.0f, 1.0f, 0.0f}};
        const vec3  position             = vec3{light->world_from_node() * vec4{0.0f, 0.0f, 0.0f, 1.0f}};
        const vec4  radiance             = vec4{light->intensity * light->color, light->range};
        const float inner_spot_cos       = std::cos(light->inner_spot_angle * 0.5f);
        const float outer_spot_cos       = std::cos(light->outer_spot_angle * 0.5f);
        const vec4  position_inner_spot  = vec4{position, inner_spot_cos};
        const vec4  direction_outer_spot = vec4{glm::normalize(vec3{direction}), outer_spot_cos};
        write(light_gpu_data, m_light_writer.write_offset + offsets.light.texture_from_world,           as_span(light->texture_from_world()));
        write(light_gpu_data, m_light_writer.write_offset + offsets.light.position_and_inner_spot_cos,  as_span(position_inner_spot));
        write(light_gpu_data, m_light_writer.write_offset + offsets.light.direction_and_outer_spot_cos, as_span(direction_outer_spot));
        write(light_gpu_data, m_light_writer.write_offset + offsets.light.radiance_and_range,           as_span(radiance));
        m_light_writer.write_offset += entry_size;
        ++light_index;
    }
    write(light_gpu_data, m_light_writer.range.first_byte_offset + offsets.directional_light_count, as_span(directional_light_count)  );
    write(light_gpu_data, m_light_writer.range.first_byte_offset + offsets.point_light_count,       as_span(point_light_count)        );
    write(light_gpu_data, m_light_writer.range.first_byte_offset + offsets.spot_light_count,        as_span(spot_light_count)         );
    write(light_gpu_data, m_light_writer.range.first_byte_offset + offsets.ambient_light,           as_span(light_layer.ambient_light));

    m_light_writer.end();

    return m_light_writer.range;
}

auto Base_renderer::update_material_buffer(
    const Material_collection& materials
) -> Buffer_range
{
    ERHE_PROFILE_FUNCTION

    log_render.trace("{}(materials.size() = {})\n", __func__, materials.size());

    const auto&  shader_resources  = *m_program_interface->shader_resources.get();
    const size_t entry_size        = shader_resources.material_struct.size_bytes();
    const auto&  offsets           = shader_resources.material_block_offsets;
    auto         material_gpu_data = current_frame_resources().material_buffer.map();
    size_t       material_index    = 0;
    m_material_writer.begin(current_frame_resources().material_buffer.target());
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

auto Base_renderer::update_camera_buffer(
    ICamera&       camera,
    const Viewport viewport
) -> Buffer_range
{
    ERHE_PROFILE_FUNCTION

    camera.update(viewport);

    const auto& shader_resources = *m_program_interface->shader_resources.get();
    auto        camera_gpu_data  = current_frame_resources().camera_buffer.map();
    const auto& offsets          = shader_resources.camera_block_offsets;
    const mat4  world_from_node  = camera.world_from_node();
    const mat4  world_from_clip  = camera.world_from_clip();
    const mat4  clip_from_world  = camera.clip_from_world();
    const float exposure         = 1.0f;

    m_camera_writer.begin(current_frame_resources().camera_buffer.target());
    const float viewport_floats[4] {
        static_cast<float>(viewport.x),
        static_cast<float>(viewport.y),
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height)
    };
    const auto fov_sides = camera.projection()->get_fov_sides(viewport);
    const float fov_floats[4] {
        fov_sides.left,
        fov_sides.right,
        fov_sides.up,
        fov_sides.down
    };
    const float clip_depth_direction = viewport.reverse_depth ? -1.0f : 1.0f;
    const float view_depth_near      = camera.projection()->z_near;
    const float view_depth_far       = camera.projection()->z_far;
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.world_from_node,      as_span(world_from_node     ));
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.world_from_clip,      as_span(world_from_clip     ));
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.clip_from_world,      as_span(clip_from_world     ));
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.viewport,             as_span(viewport_floats     ));
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.fov,                  as_span(fov_floats          ));
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.clip_depth_direction, as_span(clip_depth_direction));
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.view_depth_near,      as_span(view_depth_near     ));
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.view_depth_far,       as_span(view_depth_far      ));
    write(camera_gpu_data, m_camera_writer.write_offset + offsets.exposure,             as_span(exposure            ));
    m_camera_writer.write_offset += shader_resources.camera_block.size_bytes();
    m_camera_writer.end();

    return m_camera_writer.range;
}


auto Base_renderer::update_draw_indirect_buffer(
    const Mesh_layer&        mesh_layer,
    const Primitive_mode     primitive_mode,
    const Visibility_filter& visibility_filter
) -> Base_renderer::Draw_indirect_buffer_range
{
    ERHE_PROFILE_FUNCTION

    auto     draw_indirect_gpu_data = current_frame_resources().draw_indirect_buffer.map();
    uint32_t instance_count     {1};
    uint32_t base_instance      {0};
    size_t   draw_indirect_count{0};
    m_draw_indirect_writer.begin(current_frame_resources().draw_indirect_buffer.target());
    for (auto mesh : mesh_layer.meshes)
    {
        if (!visibility_filter(mesh->visibility_mask()))
        {
            continue;
        }
        for (auto& primitive : mesh->data.primitives)
        {
            const auto* const primitive_geometry = primitive.primitive_geometry.get();
            const auto index_range = primitive_geometry->index_range(primitive_mode);
            if (index_range.index_count == 0)
            {
                continue;
            }

            uint32_t index_count = static_cast<uint32_t>(index_range.index_count);
            if (m_max_index_count_enable)
            {
                index_count = std::min(index_count, static_cast<uint32_t>(m_max_index_count));
            }

            const uint32_t base_index  = primitive_geometry->base_index();
            const uint32_t first_index = static_cast<uint32_t>(index_range.first_index + base_index);
            const uint32_t base_vertex = primitive_geometry->base_vertex();

            const gl::Draw_elements_indirect_command draw_command{
                index_count,
                instance_count,
                first_index,
                base_vertex,
                base_instance
            };

            erhe::graphics::write(
                draw_indirect_gpu_data,
                m_draw_indirect_writer.write_offset,
                erhe::graphics::as_span(draw_command)
            );

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

    const auto& shader_resources = *m_program_interface->shader_resources.get();
    const auto& material_buffer  = current_frame_resources().material_buffer;
    gl::bind_buffer_range(
        material_buffer.target(),
        static_cast<GLuint>    (shader_resources.material_block.binding_point()),
        static_cast<GLuint>    (material_buffer.gl_name()),
        static_cast<GLintptr>  (m_material_writer.range.first_byte_offset),
        static_cast<GLsizeiptr>(m_material_writer.range.byte_count)
    );
}

void Base_renderer::bind_light_buffer()
{
    if (m_light_writer.range.byte_count == 0)
    {
        return;
    }

    const auto& shader_resources = *m_program_interface->shader_resources.get();
    const auto& buffer           = current_frame_resources().light_buffer;
    gl::bind_buffer_range(
        buffer.target(),
        static_cast<GLuint>    (shader_resources.light_block.binding_point()),
        static_cast<GLuint>    (buffer.gl_name()),
        static_cast<GLintptr>  (m_light_writer.range.first_byte_offset),
        static_cast<GLsizeiptr>(m_light_writer.range.byte_count)
    );
}

void Base_renderer::bind_camera_buffer()
{
    if (m_camera_writer.range.byte_count == 0)
    {
        return;
    }

    const auto& shader_resources = *m_program_interface->shader_resources.get();
    const auto& buffer           = current_frame_resources().camera_buffer;
    gl::bind_buffer_range(
        buffer.target(),
        static_cast<GLuint>    (shader_resources.camera_block.binding_point()),
        static_cast<GLuint>    (buffer.gl_name()),
        static_cast<GLintptr>  (m_camera_writer.range.first_byte_offset),
        static_cast<GLsizeiptr>(m_camera_writer.range.byte_count)
    );
}

void Base_renderer::bind_primitive_buffer()
{
    if (m_primitive_writer.range.byte_count == 0)
    {
        return;
    }

    const auto& shader_resources = *m_program_interface->shader_resources.get();
    const auto& buffer           = current_frame_resources().primitive_buffer;
    gl::bind_buffer_range(
        buffer.target(),
        static_cast<GLuint>    (shader_resources.primitive_block.binding_point()),
        static_cast<GLuint>    (buffer.gl_name()),
        static_cast<GLintptr>  (m_primitive_writer.range.first_byte_offset),
        static_cast<GLsizeiptr>(m_primitive_writer.range.byte_count)
    );
}

void Base_renderer::bind_draw_indirect_buffer()
{
    if (m_draw_indirect_writer.range.byte_count == 0)
    {
        return;
    }

    const auto& buffer = current_frame_resources().draw_indirect_buffer;
    gl::bind_buffer(
        buffer.target(),
        static_cast<GLuint>(buffer.gl_name())
    );
}

void Base_renderer::debug_properties_window()
{
    ImGui::Begin("Base Renderer Debug Properties");
    ImGui::Checkbox("Enable Max Index Count", &m_max_index_count_enable);
    ImGui::SliderInt("Max Index Count", &m_max_index_count, 0, 256);
    ImGui::End();
}

auto Base_renderer::max_index_count_enable() const -> bool
{
    return m_max_index_count_enable;
}

auto Base_renderer::max_index_count() const -> int
{
    return m_max_index_count;
}

} // namespace editor
