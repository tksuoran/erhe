// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/shadow_renderer.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_gl/draw_indirect.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

static constexpr std::string_view c_shadow_renderer_initialize_component{"Shadow_renderer::initialize_component()"};

Shadow_renderer::Shadow_renderer(erhe::graphics::Instance& graphics_instance, Program_interface& program_interface)
    : m_graphics_instance{graphics_instance}
    , m_shader_stages{
        graphics_instance,
        program_interface.make_prototype(
            "res/shaders",
            erhe::graphics::Shader_stages_create_info{
                .name           = "depth_only",
                .dump_interface = false,
                .build          = true
            }
        )
    }
    , m_shadow_sampler_compare{
        graphics_instance,
        erhe::graphics::Sampler_create_info{
            .min_filter   = gl::Texture_min_filter::linear,
            .mag_filter   = gl::Texture_mag_filter::linear,
            .wrap_mode    = { gl::Texture_wrap_mode::clamp_to_edge, gl::Texture_wrap_mode::clamp_to_edge, gl::Texture_wrap_mode::clamp_to_edge },
            .compare_mode = gl::Texture_compare_mode::compare_ref_to_texture,
            .compare_func = gl::Texture_compare_func::gequal,
            .lod_bias     = 0.0f,
            .max_lod      = 0.0f,
            .min_lod      = 0.0f,
            .debug_label  = "Shadow_renderer::m_shadow_sampler_compare"
        }
    }
    , m_shadow_sampler_no_compare{
        graphics_instance,
        erhe::graphics::Sampler_create_info{
            .min_filter   = gl::Texture_min_filter::linear,
            .mag_filter   = gl::Texture_mag_filter::nearest,
            .wrap_mode    = { gl::Texture_wrap_mode::clamp_to_edge, gl::Texture_wrap_mode::clamp_to_edge, gl::Texture_wrap_mode::clamp_to_edge },
            .compare_mode = gl::Texture_compare_mode::none,
            .lod_bias     = 0.0f,
            .max_lod      = 0.0f,
            .min_lod      = 0.0f,
            .debug_label  = "Shadow_renderer::m_shadow_sampler_no_compare"
        }
    }
    , m_vertex_input        {graphics_instance}
    , m_draw_indirect_buffer{graphics_instance}
    , m_joint_buffer        {graphics_instance, program_interface.joint_interface}
    , m_light_buffer        {graphics_instance, program_interface.light_interface}
    , m_primitive_buffer    {graphics_instance, program_interface.primitive_interface}
    , m_material_buffer     {graphics_instance, program_interface.material_interface}
    , m_gpu_timer           {graphics_instance, "Shadow_renderer"}
{
    m_pipeline_cache_entries.resize(8);
}

auto Shadow_renderer::get_pipeline(const Vertex_input_state* vertex_input_state) -> erhe::graphics::Pipeline&
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
    const bool reverse_depth = m_graphics_instance.configuration.reverse_depth;
    lru_entry->serial = m_pipeline_cache_serial;
    lru_entry->pipeline = erhe::graphics::Pipeline{
        erhe::graphics::Pipeline_data{
            .name           = "Shadow Renderer",
            .shader_stages  = &m_shader_stages.shader_stages,
            .vertex_input   = vertex_input_state,
            .input_assembly = Input_assembly_state::triangles,
            .rasterization  = Rasterization_state::cull_mode_none,
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
            .color_blend    = Color_blend_state::color_writes_disabled
        }
    };
    return lru_entry->pipeline;
}

static constexpr std::string_view c_shadow_renderer_render{"Shadow_renderer::render()"};

auto Shadow_renderer::render(const Render_parameters& parameters) -> bool
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(parameters.vertex_input_state != nullptr);
    ERHE_VERIFY(parameters.index_buffer != nullptr);
    ERHE_VERIFY(parameters.vertex_buffer != nullptr);
    ERHE_VERIFY(parameters.view_camera != nullptr);

    const uint64_t shadow_texture_handle_compare    = m_graphics_instance.get_handle(*parameters.texture.get(), m_shadow_sampler_compare);
    const uint64_t shadow_texture_handle_no_compare = m_graphics_instance.get_handle(*parameters.texture.get(), m_shadow_sampler_no_compare);

    // Also assigns lights slot in uniform block shader resource
    parameters.light_projections = Light_projections{
        parameters.lights,
        parameters.view_camera,
        ////parameters.view_camera_viewport,
        parameters.light_camera_viewport,
        parameters.texture,
        shadow_texture_handle_compare,
        shadow_texture_handle_no_compare
    };

    log_render->debug("Shadow_renderer::render()");
    log_shadow_renderer->trace(
        "Making light projections using texture '{}' sampler '{}' / '{}' handle '{}' / '{}'",
        parameters.texture->debug_label(),
        m_shadow_sampler_compare.debug_label(),
        m_shadow_sampler_no_compare.debug_label(),
        erhe::graphics::format_texture_handle(parameters.light_projections.shadow_map_texture_handle_compare),
        erhe::graphics::format_texture_handle(parameters.light_projections.shadow_map_texture_handle_no_compare)
    );

    //ERHE_PROFILE_GPU_SCOPE(c_shadow_renderer_render)

    erhe::graphics::Scoped_debug_group debug_group{c_shadow_renderer_render};
    erhe::graphics::Scoped_gpu_timer   timer      {m_gpu_timer};

    const auto& mesh_spans = parameters.mesh_spans;
    const auto& lights     = parameters.lights;

    auto& pipeline = get_pipeline(parameters.vertex_input_state);

    // TODO Multiple vertex buffer bindings
    m_graphics_instance.opengl_state_tracker.execute(pipeline);
    m_graphics_instance.opengl_state_tracker.vertex_input.set_index_buffer(parameters.index_buffer);
    m_graphics_instance.opengl_state_tracker.vertex_input.set_vertex_buffer(0, parameters.vertex_buffer, parameters.vertex_buffer_offset);

    gl::viewport(
        parameters.light_camera_viewport.x,
        parameters.light_camera_viewport.y,
        parameters.light_camera_viewport.width,
        parameters.light_camera_viewport.height
    );

    if ((parameters.light_camera_viewport.width > 2) && (parameters.light_camera_viewport.height > 2)) {
        gl::scissor(
            parameters.light_camera_viewport.x + 1,
            parameters.light_camera_viewport.y + 1,
            parameters.light_camera_viewport.width - 2,
            parameters.light_camera_viewport.height - 2
        );
    }

    erhe::Item_filter shadow_filter{
        .require_all_bits_set           = erhe::Item_flags::visible | erhe::Item_flags::shadow_cast,
        .require_at_least_one_bit_set   = 0u,
        .require_all_bits_clear         = 0u,
        .require_at_least_one_bit_clear = 0u
    };

    using Buffer_range = erhe::graphics::Buffer_range;
    using Draw_indirect_buffer_range = erhe::renderer::Draw_indirect_buffer_range;

    Buffer_range material_range = m_material_buffer.update(parameters.materials);
    m_material_buffer.bind(material_range);

    Buffer_range joint_range = m_joint_buffer.update(glm::uvec4{0, 0, 0, 0}, {}, parameters.skins);
    Buffer_range light_range = m_light_buffer.update(lights, &parameters.light_projections, glm::vec3{0.0f});
    m_joint_buffer.bind(joint_range);
    m_light_buffer.bind_light_buffer(light_range);

    log_shadow_renderer->trace("Rendering shadow map to '{}'", parameters.texture->debug_label());

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
        if (light_index >= parameters.framebuffers.size()) {
            continue;
        }

        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, parameters.framebuffers[light_index]->gl_name());
        gl::disable(gl::Enable_cap::scissor_test);
        gl::clear_buffer_fv(gl::Buffer::depth, 0, m_graphics_instance.depth_clear_value_pointer());
        gl::enable(gl::Enable_cap::scissor_test);

        Buffer_range control_range = m_light_buffer.update_control(light_index);
        m_light_buffer.bind_control_buffer(control_range);

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

            m_primitive_buffer.bind(primitive_range);
            m_draw_indirect_buffer.bind(draw_indirect_buffer_range.range);

            {
                static constexpr std::string_view c_id_mdi{"mdi"};

                ERHE_PROFILE_SCOPE("mdi");
                //ERHE_PROFILE_GPU_SCOPE(c_id_mdi);
                m_graphics_instance.multi_draw_elements_indirect(
                    pipeline.data.input_assembly.primitive_topology,
                    erhe::graphics::to_gl_index_type(parameters.index_type),
                    reinterpret_cast<const void *>(draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer()),
                    static_cast<GLsizei>(draw_indirect_buffer_range.draw_indirect_count),
                    static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
                );
            }
            primitive_range.release();
            draw_indirect_buffer_range.range.release();
        }

        control_range.release();
    }

    material_range.release();
    joint_range.release();
    light_range.release();

    gl::disable(gl::Enable_cap::scissor_test);

    return true;
}

} // namespace erhe::scene_renderer
