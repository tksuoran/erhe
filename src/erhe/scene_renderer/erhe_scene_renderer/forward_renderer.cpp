// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/forward_renderer.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <functional>

namespace erhe::scene_renderer {

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

Forward_renderer::Forward_renderer(erhe::graphics::Device& graphics_device, Program_interface& program_interface)
    : m_graphics_device     {graphics_device}
    , m_program_interface   {program_interface}
    , m_camera_buffer       {graphics_device, program_interface.camera_interface}
    , m_draw_indirect_buffer{graphics_device}
    , m_joint_buffer        {graphics_device, program_interface.joint_interface}
    , m_light_buffer        {graphics_device, program_interface.light_interface}
    , m_material_buffer     {graphics_device, program_interface.material_interface}
    , m_primitive_buffer    {graphics_device, program_interface.primitive_interface}
    , m_shadow_sampler_compare{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter        = erhe::graphics::Filter::linear,
            .mag_filter        = erhe::graphics::Filter::linear,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode      = { erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge },
            .compare_enable    = true,
            .compare_operation = erhe::graphics::Compare_operation::greater_or_equal,
            .lod_bias          = 0.0f,
            .max_lod           = 0.0f,
            .min_lod           = 0.0f,
            .debug_label       = "Forward_renderer::m_shadow_sampler_compare"
        }
    }
    , m_shadow_sampler_no_compare{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter        = erhe::graphics::Filter::linear,
            .mag_filter        = erhe::graphics::Filter::nearest,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode      = { erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge },
            .compare_enable    = false,
            .compare_operation = erhe::graphics::Compare_operation::always,
            .lod_bias          = 0.0f,
            .max_lod           = 0.0f,
            .min_lod           = 0.0f,
            .debug_label       = "Forward_renderer::m_shadow_sampler_no_compare"
        }
    }
    , m_fallback_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter        = erhe::graphics::Filter::nearest,
            .mag_filter        = erhe::graphics::Filter::nearest,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode      = { erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge },
            .compare_enable    = false,
            .compare_operation = erhe::graphics::Compare_operation::always,
            .debug_label       = "Forward_renderer::m_fallback_sampler"
        }
    }
{
    m_dummy_texture = graphics_device.create_dummy_texture();
}

static constexpr std::string_view c_forward_renderer_render{"Forward_renderer::render()"};

namespace {

const char* safe_str(const char* str)
{
    return str != nullptr ? str : "";
}

}

void Forward_renderer::render(const Render_parameters& parameters)
{
    ERHE_PROFILE_FUNCTION();

    log_render->debug("Forward_renderer::render({})", parameters.debug_label);

    const auto& viewport       = parameters.viewport;
    const auto* camera         = parameters.camera;
    const auto& mesh_spans     = parameters.mesh_spans;
    const auto& lights         = parameters.lights;
    const auto& skins          = parameters.skins;
    const auto& materials      = parameters.materials;
    const auto& passes         = parameters.passes;
    const auto& filter         = parameters.filter;
    const auto  primitive_mode = parameters.primitive_mode;

    using Buffer_range = erhe::graphics::Buffer_range;
    std::optional<Buffer_range> camera_buffer_range{};
    if (camera != nullptr) {
        camera_buffer_range = m_camera_buffer.update(
            *camera->projection(),
            *camera->get_node(),
            viewport,
            camera->get_exposure(),
            parameters.grid_size,
            parameters.grid_line_width,
            parameters.frame_number
        );
        m_camera_buffer.bind(parameters.render_encoder, camera_buffer_range.value());
    }

    erhe::graphics::Texture_heap texture_heap{
        m_graphics_device,
        *m_dummy_texture.get(),
        m_fallback_sampler,
        erhe::scene_renderer::c_texture_heap_slot_count_reserved
    };

    Buffer_range material_range = m_material_buffer.update(texture_heap, materials);
    m_material_buffer.bind(parameters.render_encoder, material_range);

    Buffer_range joint_range = m_joint_buffer.update(parameters.debug_joint_indices, parameters.debug_joint_colors, skins);
    m_joint_buffer.bind(parameters.render_encoder, joint_range);

    // This must be done even if lights is empty.
    // For example, the number of lights is read from the light buffer.
    Buffer_range light_range = m_light_buffer.update(lights, parameters.light_projections, parameters.ambient_light, texture_heap);
    m_light_buffer.bind_light_buffer(parameters.render_encoder, light_range);

    texture_heap.bind();

    for (auto& pass : passes) {
        const auto& pipeline = pass->pipeline;
        bool use_override_shader_stages = (parameters.override_shader_stages != nullptr);
        if ((pipeline.data.shader_stages == nullptr) && !use_override_shader_stages) {
            continue;
        }

        auto* used_shader_stages = use_override_shader_stages ? parameters.override_shader_stages : pipeline.data.shader_stages;
        if (!used_shader_stages->is_valid()) {
            use_override_shader_stages = true;
            used_shader_stages = parameters.error_shader_stages;
        }

        if (pass->begin) {
            ERHE_PROFILE_SCOPE("pass begin");
            pass->begin();
        }

        const std::string debug_group_name = fmt::format("Forward_renderer::render() pass: {}", pipeline.data.name);
        erhe::graphics::Scoped_debug_group pass_scope{debug_group_name};

        if (use_override_shader_stages) {
            parameters.render_encoder.set_render_pipeline_state(pipeline, used_shader_stages);
        } else {
            parameters.render_encoder.set_render_pipeline_state(pipeline);
        }
        parameters.render_encoder.set_index_buffer(parameters.index_buffer);
        parameters.render_encoder.set_vertex_buffer(parameters.vertex_buffer0, 0, 0);
        parameters.render_encoder.set_vertex_buffer(parameters.vertex_buffer1, 0, 1);

        for (const auto& meshes : mesh_spans) {
            ERHE_PROFILE_SCOPE("mesh span");
            //ERHE_PROFILE_GPU_SCOPE(c_forward_renderer_render);
            if (meshes.empty()) {
                continue;
            }

            std::size_t primitive_count{0};
            Buffer_range primitive_range = m_primitive_buffer.update(meshes, primitive_mode, filter, parameters.primitive_settings, primitive_count);
            if (primitive_count == 0){
                primitive_range.cancel();
                continue;
            }
            erhe::renderer::Draw_indirect_buffer_range draw_indirect_buffer_range = m_draw_indirect_buffer.update(meshes, primitive_mode, filter);
            if (draw_indirect_buffer_range.draw_indirect_count == 0) {
                primitive_range.cancel();
                draw_indirect_buffer_range.range.cancel();
                continue;
            }
            ERHE_VERIFY(primitive_count == draw_indirect_buffer_range.draw_indirect_count);
            m_primitive_buffer.bind(parameters.render_encoder, primitive_range);
            m_draw_indirect_buffer.bind(parameters.render_encoder, draw_indirect_buffer_range.range); // Draw indirect buffer is not indexed, this binds the whole buffer

            parameters.render_encoder.multi_draw_indexed_primitives_indirect(
                pipeline.data.input_assembly.primitive_topology,
                parameters.index_type,
                draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer(),
                draw_indirect_buffer_range.draw_indirect_count,
                sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
            );

            primitive_range.release();
            draw_indirect_buffer_range.range.release();
        }

        if (pass->end) {
            ERHE_PROFILE_SCOPE("pass end");
            pass->end();
        }
    }

    // These must come after the draw calls have been done
    if (camera_buffer_range.has_value()) {
        camera_buffer_range.value().release();
    }
    material_range.release();
    joint_range.release();
    light_range.release();

    texture_heap.unbind();
}

void Forward_renderer::draw_primitives(const Render_parameters& parameters, const erhe::scene::Light* light)
{
    ERHE_PROFILE_FUNCTION();

    const auto& viewport = parameters.viewport;
    const auto* camera   = parameters.camera;
    const auto& lights   = parameters.lights;
    const auto& passes   = parameters.passes;

    erhe::graphics::Texture_heap texture_heap{
        m_graphics_device,
        *m_dummy_texture.get(),
        m_fallback_sampler,
        erhe::scene_renderer::c_texture_heap_slot_count_reserved
    };

    using Buffer_range = erhe::graphics::Buffer_range;
    Buffer_range material_range = m_material_buffer.update(texture_heap, parameters.materials);
    if (material_range.get_buffer() != nullptr) {
        m_material_buffer.bind(parameters.render_encoder, material_range);
    }

    std::optional<Buffer_range> camera_range;
    if (camera != nullptr) {
        camera_range = m_camera_buffer.update(
            *camera->projection(),
            *camera->get_node(),
            viewport,
            camera->get_exposure(),
            parameters.grid_size,
            parameters.grid_line_width,
            parameters.frame_number
        );
        m_camera_buffer.bind(parameters.render_encoder, camera_range.value());
    }

    std::optional<Buffer_range> light_control_range{};
    if (light != nullptr) {
        const auto* light_projection_transforms = parameters.light_projections->get_light_projection_transforms_for_light(light);
        if (light_projection_transforms != nullptr) {
            light_control_range = m_light_buffer.update_control(light_projection_transforms->index);
            m_light_buffer.bind_control_buffer(parameters.render_encoder, light_control_range.value());
        } else {
            //// log_render->warn("Light {} has no light projection transforms", light->name());
        }
    }

    Buffer_range light_range = m_light_buffer.update(lights, parameters.light_projections, parameters.ambient_light, texture_heap);
    m_light_buffer.bind_light_buffer(parameters.render_encoder, light_range);

    texture_heap.bind();

    for (auto& pass : passes) {
        const auto& pipeline = pass->pipeline;
        if (!pipeline.data.shader_stages) {
            continue;
        }

        if (pass->begin) {
            pass->begin();
        }

        const std::string debug_group_name = fmt::format("Forward_renderer::draw_primitives() pass: {}", pipeline.data.name);
        erhe::graphics::Scoped_debug_group pass_scope{debug_group_name};

        parameters.render_encoder.set_render_pipeline_state(pipeline);
        parameters.render_encoder.draw_primitives(pipeline.data.input_assembly.primitive_topology, 0, parameters.non_mesh_vertex_count);

        if (pass->end) {
            pass->end();
        }
    }

    material_range.release();
    light_range.release();

    if (light_control_range.has_value()) {
        light_control_range.value().release();
    }
    if (camera_range.has_value()) {
        camera_range.value().release();
    }

    texture_heap.unbind();
}

} // namespace erhe::scene_renderer
