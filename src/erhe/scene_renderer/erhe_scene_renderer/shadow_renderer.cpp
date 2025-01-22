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
        program_interface.make_prototype(
            graphics_instance,
            "res/shaders",
            erhe::graphics::Shader_stages_create_info{
                .name           = "depth_only",
                .dump_interface = false,
                .build          = true
            }
        )
    }
    , m_nearest_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::nearest_mipmap_nearest,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .debug_label = "Shadow_renderer"
        }
    }
    , m_draw_indirect_buffers{graphics_instance}
    , m_joint_buffers        {graphics_instance, program_interface.joint_interface}
    , m_light_buffers        {graphics_instance, program_interface.light_interface}
    , m_primitive_buffers    {graphics_instance, program_interface.primitive_interface}
    , m_gpu_timer            {"Shadow_renderer"}
{
    ERHE_PROFILE_FUNCTION();
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
    lru_entry->pipeline.data = {
        .name           = "Shadow Renderer",
        .shader_stages  = &m_shader_stages.shader_stages,
        .vertex_input   = vertex_input_state,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_none,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_writes_disabled
    };
    return lru_entry->pipeline;
}

static constexpr std::string_view c_shadow_renderer_render{"Shadow_renderer::render()"};

auto Shadow_renderer::render(const Render_parameters& parameters) -> bool
{
    ERHE_PROFILE_FUNCTION();

    log_render->debug("Shadow_renderer::render()");
    log_shadow_renderer->trace(
        "Making light projections using texture '{}' sampler '{}' handle '{}'",
        parameters.texture->debug_label(),
        m_nearest_sampler.debug_label(),
        erhe::graphics::format_texture_handle(parameters.light_projections.shadow_map_texture_handle)
    );
    const auto shadow_texture_handle = m_graphics_instance.get_handle(*parameters.texture.get(), m_nearest_sampler);

    // Also assigns lights slot in uniform block shader resource
    parameters.light_projections = Light_projections{
        parameters.lights,
        parameters.view_camera,
        ////parameters.view_camera_viewport,
        parameters.light_camera_viewport,
        parameters.texture,
        shadow_texture_handle
    };

    //ERHE_PROFILE_GPU_SCOPE(c_shadow_renderer_render)

    erhe::graphics::Scoped_debug_group debug_group{c_shadow_renderer_render};
    erhe::graphics::Scoped_gpu_timer   timer      {m_gpu_timer};

    const auto& mesh_spans = parameters.mesh_spans;
    const auto& lights     = parameters.lights;

    auto& pipeline = get_pipeline(parameters.vertex_input_state);

    m_graphics_instance.opengl_state_tracker.execute(pipeline);
    gl::viewport(
        parameters.light_camera_viewport.x,
        parameters.light_camera_viewport.y,
        parameters.light_camera_viewport.width,
        parameters.light_camera_viewport.height
    );

    erhe::Item_filter shadow_filter{
        .require_all_bits_set           =
            erhe::Item_flags::visible |
            erhe::Item_flags::shadow_cast,
        .require_at_least_one_bit_set   = 0u,
        .require_all_bits_clear         = 0u,
        .require_at_least_one_bit_clear = 0u
    };

    const auto joint_range = m_joint_buffers.update(
        glm::uvec4{0, 0, 0, 0},
        {},
        parameters.skins
    );
    m_joint_buffers.bind(joint_range);

    const auto light_range = m_light_buffers.update(
        lights,
        &parameters.light_projections,
        glm::vec3{0.0f}
    );
    m_light_buffers.bind_light_buffer(light_range);

    log_shadow_renderer->trace("Rendering shadow map to '{}'", parameters.texture->debug_label());

    const erhe::primitive::Primitive_mode primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
    for (const auto& meshes : mesh_spans) {
        std::size_t primitive_count{0};
        const auto primitive_range = m_primitive_buffers.update(meshes, primitive_mode, shadow_filter, Primitive_interface_settings{}, primitive_count);
        const auto draw_indirect_buffer_range = m_draw_indirect_buffers.update(meshes, primitive_mode, shadow_filter);
        if (primitive_count != draw_indirect_buffer_range.draw_indirect_count) {
            log_render->warn("primitive_range != draw_indirect_buffer_range.draw_indirect_count");
        }

        if (draw_indirect_buffer_range.draw_indirect_count > 0) {
            m_primitive_buffers.bind(primitive_range);
            m_draw_indirect_buffers.bind(draw_indirect_buffer_range.range);
        }

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

            {
                ERHE_PROFILE_SCOPE("bind fbo");
                gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, parameters.framebuffers[light_index]->gl_name());
            }

            {
                static constexpr std::string_view c_id_clear{"clear"};

                ERHE_PROFILE_SCOPE("clear fbo");
                //ERHE_PROFILE_GPU_SCOPE(c_id_clear);

                gl::clear_buffer_fv(
                    gl::Buffer::depth,
                    0,
                    m_graphics_instance.depth_clear_value_pointer()
                );
            }

            if (draw_indirect_buffer_range.draw_indirect_count == 0) {
                continue;
            }

            const auto control_range = m_light_buffers.update_control(light_index);
            m_light_buffers.bind_control_buffer(control_range);

            {
                static constexpr std::string_view c_id_mdi{"mdi"};

                ERHE_PROFILE_SCOPE("mdi");
                //ERHE_PROFILE_GPU_SCOPE(c_id_mdi);
                gl::multi_draw_elements_indirect(
                    pipeline.data.input_assembly.primitive_topology,
                    erhe::graphics::to_gl_index_type(parameters.index_type),
                    nullptr, //reinterpret_cast<const void *>(draw_indirect_buffer_range.range.first_byte_offset),
                    static_cast<GLsizei>(draw_indirect_buffer_range.draw_indirect_count),
                    static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
                );
            }
        }
    }
    return true;
}

} // namespace erhe::scene_renderer
