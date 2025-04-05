// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/forward_renderer.hpp"

#include "erhe_gl/draw_indirect.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"
#include "erhe_profile/profile.hpp"

#include <functional>

namespace erhe::scene_renderer {

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

Forward_renderer::Forward_renderer(erhe::graphics::Instance& graphics_instance, Program_interface& program_interface)
    : m_graphics_instance   {graphics_instance}
    , m_program_interface   {program_interface}
    , m_camera_buffer       {graphics_instance, program_interface.camera_interface}
    , m_draw_indirect_buffer{graphics_instance}
    , m_joint_buffer        {graphics_instance, program_interface.joint_interface}
    , m_light_buffer        {graphics_instance, program_interface.light_interface}
    , m_material_buffer     {graphics_instance, program_interface.material_interface}
    , m_primitive_buffer    {graphics_instance, program_interface.primitive_interface}
    , m_nearest_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::nearest_mipmap_nearest,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .debug_label = "Forward_aer nearest"
        }
    }
{
    static constexpr std::string_view c_forward_renderer_initialize_component{"Forward_renderer::initialize_component()"};
    ERHE_PROFILE_FUNCTION();

    erhe::graphics::Scoped_debug_group forward_renderer_initialization{c_forward_renderer_initialize_component};

    m_dummy_texture = graphics_instance.create_dummy_texture();
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
    const bool  enable_shadows =
        (!lights.empty()) &&
        (parameters.shadow_texture != nullptr) &&
        (parameters.light_projections->shadow_map_texture_handle != erhe::graphics::invalid_texture_handle);

    const uint64_t shadow_texture_handle = enable_shadows
        ? parameters.light_projections->shadow_map_texture_handle
        : erhe::graphics::invalid_texture_handle;
    const uint64_t fallback_texture_handle = m_graphics_instance.get_handle(*m_dummy_texture.get(), m_nearest_sampler);

    log_forward_renderer->trace(
        "render({}) shadow T '{}' handle {}",
        safe_str(parameters.passes.front()->pipeline.data.name),
        enable_shadows ? parameters.shadow_texture->debug_label() : "",
        erhe::graphics::format_texture_handle(shadow_texture_handle)
    );

    gl::viewport(viewport.x, viewport.y, viewport.width, viewport.height);

    using Buffer_range = erhe::renderer::Buffer_range;
    std::optional<Buffer_range> camera_buffer_range{};
    if (camera != nullptr) {
        camera_buffer_range = m_camera_buffer.update(
            *camera->projection(),
            *camera->get_node(),
            viewport,
            camera->get_exposure(),
            parameters.grid_size,
            parameters.grid_line_width
        );
        camera_buffer_range.value().bind();
    }

    if (!m_graphics_instance.info.use_bindless_texture) {
        m_graphics_instance.texture_unit_cache_reset(m_base_texture_unit);
    }

    Buffer_range material_range = m_material_buffer.update(materials);
    material_range.bind();

    Buffer_range joint_range = m_joint_buffer.update(parameters.debug_joint_indices, parameters.debug_joint_colors, skins);
    joint_range.bind();

    // This must be done even if lights is empty.
    // For example, the number of lights is read from the light buffer.
    Buffer_range light_range = m_light_buffer.update(lights, parameters.light_projections, parameters.ambient_light);
    light_range.bind();

    if (m_graphics_instance.info.use_bindless_texture) {
        ERHE_PROFILE_SCOPE("make textures resident");

        if (enable_shadows) {
            gl::make_texture_handle_resident_arb(shadow_texture_handle);
        }
        for (const uint64_t handle : m_material_buffer.used_handles()) {
            gl::make_texture_handle_resident_arb(handle);
        }
    } else {
        ERHE_PROFILE_SCOPE("bind texture units");

        if (enable_shadows) {
            gl::bind_texture_unit(Shadow_renderer::shadow_texture_unit, parameters.shadow_texture->gl_name());
            gl::bind_sampler     (Shadow_renderer::shadow_texture_unit, m_nearest_sampler.gl_name());
        }

        m_graphics_instance.texture_unit_cache_bind(fallback_texture_handle);
    }

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

        erhe::graphics::Scoped_debug_group pass_scope{pass->pipeline.data.name};

        if (use_override_shader_stages) {
            m_graphics_instance.opengl_state_tracker.shader_stages.execute(used_shader_stages);
        }
        m_graphics_instance.opengl_state_tracker.execute(pipeline, use_override_shader_stages);
        m_graphics_instance.opengl_state_tracker.vertex_input.set_index_buffer(parameters.index_buffer);
        m_graphics_instance.opengl_state_tracker.vertex_input.set_vertex_buffer(0, parameters.vertex_buffer0, 0);
        m_graphics_instance.opengl_state_tracker.vertex_input.set_vertex_buffer(1, parameters.vertex_buffer1, 0);

        for (const auto& meshes : mesh_spans) {
            ERHE_PROFILE_SCOPE("mesh span");
            //ERHE_PROFILE_GPU_SCOPE(c_forward_renderer_render);
            if (meshes.empty()) {
                continue;
            }

            std::size_t primitive_count{0};
            Buffer_range primitive_range = m_primitive_buffer.update(meshes, primitive_mode, filter, parameters.primitive_settings, primitive_count);
            erhe::renderer::Draw_indirect_buffer_range draw_indirect_buffer_range = m_draw_indirect_buffer.update(meshes, primitive_mode, filter);
            if (draw_indirect_buffer_range.draw_indirect_count == 0) {
                primitive_range.cancel();
                draw_indirect_buffer_range.range.cancel();
                continue;
            }
            ERHE_VERIFY(primitive_count == draw_indirect_buffer_range.draw_indirect_count);
            primitive_range.bind();
            draw_indirect_buffer_range.range.bind(); // Draw indirect buffer is not indexed, this binds the whole buffer

            gl::multi_draw_elements_indirect(
                pipeline.data.input_assembly.primitive_topology,
                erhe::graphics::to_gl_index_type(parameters.index_type),
                reinterpret_cast<const void *>(draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer()),
                static_cast<GLsizei>(draw_indirect_buffer_range.draw_indirect_count),
                static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
            );

            primitive_range.submit();
            draw_indirect_buffer_range.range.submit();
        }

        if (pass->end) {
            ERHE_PROFILE_SCOPE("pass end");
            pass->end();
        }
    }

    // These must come after the draw calls have been done
    if (camera_buffer_range.has_value()) {
        camera_buffer_range.value().submit();
    }
    material_range.submit();
    joint_range.submit();
    light_range.submit();

    if (m_graphics_instance.info.use_bindless_texture) {
        ERHE_PROFILE_SCOPE("make textures non resident");

        if (enable_shadows) {
            gl::make_texture_handle_non_resident_arb(shadow_texture_handle);
        }
        for (const uint64_t handle : m_material_buffer.used_handles()) {
            gl::make_texture_handle_non_resident_arb(handle);
        }
    }
}

void Forward_renderer::draw_primitives(const Render_parameters& parameters, const erhe::scene::Light* light)
{
    ERHE_PROFILE_FUNCTION();

    const auto& viewport       = parameters.viewport;
    const auto* camera         = parameters.camera;
    const auto& lights         = parameters.lights;
    const auto& passes         = parameters.passes;
    const bool  enable_shadows =
        //(g_shadow_renderer != nullptr) &&
        (!lights.empty()) &&
        (parameters.shadow_texture != nullptr);

    const uint64_t shadow_texture_handle = enable_shadows ? m_graphics_instance.get_handle(*parameters.shadow_texture, m_nearest_sampler) : 0;

    erhe::graphics::Scoped_debug_group forward_renderer_render{c_forward_renderer_render};

    gl::viewport(viewport.x, viewport.y, viewport.width, viewport.height);

    using Buffer_range = erhe::renderer::Buffer_range;
    Buffer_range material_range = m_material_buffer.update(parameters.materials);
    material_range.bind();

    std::optional<Buffer_range> camera_range;
    if (camera != nullptr) {
        camera_range = m_camera_buffer.update(
            *camera->projection(),
            *camera->get_node(),
            viewport,
            camera->get_exposure(),
            parameters.grid_size,
            parameters.grid_line_width
        );
        camera_range.value().bind();
    }

    std::optional<Buffer_range> light_control_range{};
    if (light != nullptr) {
        const auto* light_projection_transforms = parameters.light_projections->get_light_projection_transforms_for_light(light);
        if (light_projection_transforms != nullptr) {
            light_control_range = m_light_buffer.update_control(light_projection_transforms->index);
            light_control_range.value().bind();
        } else {
            //// log_render->warn("Light {} has no light projection transforms", light->name());
        }
    }

    Buffer_range light_range = m_light_buffer.update(lights, parameters.light_projections, parameters.ambient_light);
    light_range.bind();

    if (enable_shadows) {
        if (m_graphics_instance.info.use_bindless_texture) {
            gl::make_texture_handle_resident_arb(shadow_texture_handle);
        } else {
            gl::bind_texture_unit(Shadow_renderer::shadow_texture_unit, parameters.shadow_texture->gl_name());
            gl::bind_sampler     (Shadow_renderer::shadow_texture_unit, m_nearest_sampler.gl_name());
        }
    }

    for (auto& pass : passes) {
        const auto& pipeline = pass->pipeline;
        if (!pipeline.data.shader_stages) {
            continue;
        }

        if (pass->begin) {
            pass->begin();
        }

        erhe::graphics::Scoped_debug_group pass_scope{pass->pipeline.data.name};

        m_graphics_instance.opengl_state_tracker.execute(pipeline);
        gl::draw_arrays(pipeline.data.input_assembly.primitive_topology, 0, static_cast<GLsizei>(parameters.non_mesh_vertex_count));

        if (pass->end) {
            pass->end();
        }
    }

    material_range.submit();
    light_range.submit();

    if (light_control_range.has_value()) {
        light_control_range.value().submit();
    }
    if (camera_range.has_value()) {
        camera_range.value().submit();
    }

    if (enable_shadows) {
        if (m_graphics_instance.info.use_bindless_texture) {
            gl::make_texture_handle_non_resident_arb(shadow_texture_handle);
        }
    }
}

} // namespace erhe::scene_renderer
