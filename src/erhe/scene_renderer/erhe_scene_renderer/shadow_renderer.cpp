// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/shadow_renderer.hpp"

#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

using erhe::graphics::Render_pass;
using erhe::graphics::Texture;

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

static constexpr std::string_view c_shadow_renderer_initialize_component{"Shadow_renderer::initialize_component()"};

Shadow_renderer::Shadow_renderer(erhe::graphics::Device& graphics_device, Program_interface& program_interface)
    : m_graphics_device{graphics_device}
    , m_shader_stages{
        graphics_device,
        program_interface.make_prototype(
            "res/shaders",
            erhe::graphics::Shader_stages_create_info{
                .name           = "depth_only",
                .dump_interface = false,
                .build          = true
            }
        )
    }
    , m_dummy_texture{graphics_device.create_dummy_texture()}
    , m_fallback_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter        = erhe::graphics::Filter::nearest,
            .mag_filter        = erhe::graphics::Filter::nearest,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode      = { erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge },
            .compare_enable    = false,
            .compare_operation = erhe::graphics::Compare_operation::always,
            .debug_label       = "Shadow_renderer::m_fallback_sampler"
        }
    }
    , m_vertex_input        {graphics_device}
    , m_draw_indirect_buffer{graphics_device}
    , m_joint_buffer        {graphics_device, program_interface.joint_interface}
    , m_light_buffer        {graphics_device, program_interface.light_interface}
    , m_primitive_buffer    {graphics_device, program_interface.primitive_interface}
    , m_material_buffer     {graphics_device, program_interface.material_interface}
    , m_gpu_timer           {graphics_device, "Shadow_renderer"}
{
    m_pipeline_cache_entries.resize(8);
}

auto Shadow_renderer::get_pipeline(const Vertex_input_state* vertex_input_state) -> erhe::graphics::Render_pipeline_state&
{
    ERHE_PROFILE_FUNCTION();

    ++m_pipeline_cache_serial;
    uint64_t              lru_serial{m_pipeline_cache_serial};
    Pipeline_cache_entry* lru_entry {nullptr};
    for (auto& entry : m_pipeline_cache_entries) {
        if (entry.pipeline.data.vertex_input == vertex_input_state) {
            entry.serial = m_pipeline_cache_serial;
            return entry.pipeline;
        }
        if (entry.serial < lru_serial) {
            lru_serial = entry.serial;
            lru_entry = &entry;
        }
    }
    ERHE_VERIFY(lru_entry != nullptr);
    lru_entry->serial = m_pipeline_cache_serial;
    lru_entry->pipeline = erhe::graphics::Render_pipeline_state{
        erhe::graphics::Render_pipeline_data{
            .name           = "Shadow Renderer",
            .shader_stages  = &m_shader_stages.shader_stages,
            .vertex_input   = vertex_input_state,
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_none,
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
            .color_blend    = Color_blend_state::color_writes_disabled
        }
    };
    return lru_entry->pipeline;
}

auto Shadow_renderer::render(const Render_parameters& parameters) -> bool
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(parameters.vertex_input_state != nullptr);
    ERHE_VERIFY(parameters.index_buffer != nullptr);
    ERHE_VERIFY(parameters.vertex_buffer != nullptr);
    ERHE_VERIFY(parameters.view_camera != nullptr);
    ERHE_VERIFY(parameters.texture);

    // Also assigns lights slot in uniform block shader resource
    parameters.light_projections = Light_projections{
        parameters.lights,
        parameters.view_camera,
        parameters.view_camera_viewport,
        parameters.light_camera_viewport,
        parameters.texture
    };

    erhe::graphics::Scoped_debug_group debug_group{"Shadow_renderer::render()"};
    erhe::graphics::Scoped_gpu_timer   timer      {m_gpu_timer};

    const auto& mesh_spans = parameters.mesh_spans;
    const auto& lights     = parameters.lights;

    auto& pipeline = get_pipeline(parameters.vertex_input_state);

    erhe::Item_filter shadow_filter{
        .require_all_bits_set           = erhe::Item_flags::visible | erhe::Item_flags::shadow_cast,
        .require_at_least_one_bit_set   = 0u,
        .require_all_bits_clear         = 0u,
        .require_at_least_one_bit_clear = 0u
    };

    using Buffer_range = erhe::graphics::Buffer_range;
    using Draw_indirect_buffer_range = erhe::renderer::Draw_indirect_buffer_range;

    erhe::graphics::Texture_heap texture_heap{
        m_graphics_device,
        *m_dummy_texture.get(),
        m_fallback_sampler,
        erhe::scene_renderer::c_texture_heap_slot_count_reserved
    };

    Buffer_range material_range = m_material_buffer.update(texture_heap, parameters.materials);
    Buffer_range joint_range = m_joint_buffer.update(glm::uvec4{0, 0, 0, 0}, {}, parameters.skins);
    Buffer_range light_range = m_light_buffer.update(lights, &parameters.light_projections, glm::vec3{0.0f}, texture_heap);

    log_shadow_renderer->trace("Rendering shadow map to '{}'", parameters.texture->get_debug_label());

    const erhe::primitive::Primitive_mode primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};

    for (const auto& light : lights) {
        if (!light->cast_shadow) {
            continue;
        }

        auto* light_projection_transform = parameters.light_projections.get_light_projection_transforms_for_light(light.get());
        if (light_projection_transform == nullptr) {
            //// log_render->warn("Light {} has no light projection transforms", light->name());
            continue;
        }
        const std::size_t light_index = light_projection_transform->index;
        if (light_index >= parameters.render_passes.size()) {
            continue;
        }

        erhe::graphics::Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(*parameters.render_passes[light_index].get());

        // TODO Multiple vertex buffer bindings
        encoder.set_render_pipeline_state(pipeline);
        encoder.set_index_buffer(parameters.index_buffer);
        encoder.set_vertex_buffer(parameters.vertex_buffer, parameters.vertex_buffer_offset, 0);
        m_material_buffer.bind(encoder, material_range);
        m_joint_buffer.bind(encoder, joint_range);
        m_light_buffer.bind_light_buffer(encoder, light_range);

        if ((parameters.light_camera_viewport.width > 2) && (parameters.light_camera_viewport.height > 2)) {
            encoder.set_scissor_rect(
                parameters.light_camera_viewport.x + 1,
                parameters.light_camera_viewport.y + 1,
                parameters.light_camera_viewport.width - 2,
                parameters.light_camera_viewport.height - 2
            );
        }

        Buffer_range control_range = m_light_buffer.update_control(light_index);
        m_light_buffer.bind_control_buffer(encoder, control_range);

        texture_heap.bind();

        for (const auto& meshes : mesh_spans) {
            std::size_t primitive_count{0};
            Buffer_range primitive_range = m_primitive_buffer.update(meshes, primitive_mode, shadow_filter, Primitive_interface_settings{}, primitive_count);
            if (primitive_count == 0) {
                primitive_range.cancel();
                continue;
            }
            Draw_indirect_buffer_range draw_indirect_buffer_range = m_draw_indirect_buffer.update(meshes, primitive_mode, shadow_filter);
            ERHE_VERIFY(primitive_count == draw_indirect_buffer_range.draw_indirect_count);
            if (primitive_count == 0) {
                primitive_range.cancel();
                draw_indirect_buffer_range.range.cancel();
                continue;
            }

            m_primitive_buffer.bind(encoder, primitive_range);
            m_draw_indirect_buffer.bind(encoder, draw_indirect_buffer_range.range);

            {
                static constexpr std::string_view c_id_mdi{"mdi"};

                ERHE_PROFILE_SCOPE("mdi");
                //ERHE_PROFILE_GPU_SCOPE(c_id_mdi);
                encoder.multi_draw_indexed_primitives_indirect(
                    pipeline.data.input_assembly.primitive_topology,
                    parameters.index_type,
                    draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer(),
                    draw_indirect_buffer_range.draw_indirect_count,
                    sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
                );
            }
            primitive_range.release();
            draw_indirect_buffer_range.range.release();
        }

        control_range.release();
    }

    texture_heap.unbind();

    material_range.release();
    joint_range.release();
    light_range.release();

    return true;
}

} // namespace erhe::scene_renderer
