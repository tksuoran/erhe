#include "renderers/forward_renderer.hpp"

#include "renderers/mesh_memory.hpp"
#include "renderers/program_interface.hpp"
#include "renderers/programs.hpp"
#include "renderers/shadow_renderer.hpp"
#include "log.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <functional>

namespace editor
{

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

Forward_renderer::Forward_renderer()
    : Component{c_label}
{
}

Forward_renderer::~Forward_renderer() noexcept
{
}

void Forward_renderer::declare_required_components()
{
    require<erhe::application::Gl_context_provider>();
    require<Program_interface>();

    m_configuration = require<erhe::application::Configuration>();
    m_mesh_memory   = require<Mesh_memory>();
    m_programs      = require<Programs   >();
    require<Program_interface>();
}

static constexpr std::string_view c_forward_renderer_initialize_component{"Forward_renderer::initialize_component()"};
void Forward_renderer::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const erhe::application::Scoped_gl_context gl_context{
        Component::get<erhe::application::Gl_context_provider>()
    };

    erhe::graphics::Scoped_debug_group forward_renderer_initialization{c_forward_renderer_initialize_component};

    const auto& shader_resources = *get<Program_interface>()->shader_resources.get();
    m_material_buffers      = std::make_unique<Material_buffer     >(shader_resources.material_interface);
    m_light_buffers         = std::make_unique<Light_buffer        >(shader_resources.light_interface);
    m_camera_buffers        = std::make_unique<Camera_buffer       >(shader_resources.camera_interface);
    m_draw_indirect_buffers = std::make_unique<Draw_indirect_buffer>(m_configuration->renderer.max_draw_count);
    m_primitive_buffers     = std::make_unique<Primitive_buffer    >(shader_resources.primitive_interface);
}

void Forward_renderer::post_initialize()
{
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();

    if (m_configuration->shadow_renderer.enabled)
    {
        m_shadow_renderer = get<Shadow_renderer>();
    }
}

static constexpr std::string_view c_forward_renderer_render{"Forward_renderer::render()"};

void Forward_renderer::next_frame()
{
    m_material_buffers     ->next_frame();
    m_light_buffers        ->next_frame();
    m_camera_buffers       ->next_frame();
    m_draw_indirect_buffers->next_frame();
    m_primitive_buffers    ->next_frame();
}

auto Forward_renderer::primitive_settings() const -> Primitive_interface_settings&
{
    return m_primitive_buffers->settings;
}

void Forward_renderer::render(const Render_parameters& parameters)
{
    ERHE_PROFILE_FUNCTION

    const auto&    viewport              = parameters.viewport;
    const auto*    camera                = parameters.camera;
    const auto&    mesh_spans            = parameters.mesh_spans;
    const auto&    lights                = parameters.lights;
    const auto&    materials             = parameters.materials;
    const auto&    passes                = parameters.passes;
    const auto&    visibility_filter     = parameters.visibility_filter;
    const bool     enable_shadows        = m_shadow_renderer && (!lights.empty());
    const uint64_t shadow_texture_handle = enable_shadows
        ?
            erhe::graphics::get_handle(
                *m_shadow_renderer->texture(),
                *m_programs->nearest_sampler.get()
            )
        : 0;

    erhe::graphics::Scoped_debug_group forward_renderer_render{c_forward_renderer_render};

    gl::viewport(viewport.x, viewport.y, viewport.width, viewport.height);
    if (camera != nullptr)
    {
        m_camera_buffers->update(*camera->projection(), *camera, viewport, camera->get_exposure());
        m_camera_buffers->bind();
    }
    m_material_buffers->update(materials, m_programs);
    m_material_buffers->bind();

    if (!lights.empty())
    {
        m_light_buffers->update(lights, parameters.light_projections, parameters.ambient_light);
        m_light_buffers->bind_light_buffer();
    }

    if (enable_shadows)
    {
        if (erhe::graphics::Instance::info.use_bindless_texture)
        {
            ERHE_PROFILE_SCOPE("shadow texture resident");
            gl::make_texture_handle_resident_arb(shadow_texture_handle);
        }
        else
        {
            gl::bind_sampler     (m_programs->shadow_texture_unit, m_programs->nearest_sampler->gl_name());
            gl::bind_texture_unit(m_programs->shadow_texture_unit, m_shadow_renderer->texture()->gl_name());
        }
    }

    for (auto& pass : passes)
    {
        const auto& pipeline = pass->pipeline;
        if (!pipeline.data.shader_stages)
        {
            continue;
        }

        const auto primitive_mode = pass->primitive_mode; //select_primitive_mode(pass);

        if (pass->begin)
        {
            ERHE_PROFILE_SCOPE("pass begin");
            pass->begin();
        }

        erhe::graphics::Scoped_debug_group pass_scope{pass->pipeline.data.name};

        m_pipeline_state_tracker->execute(pipeline);

        for (const auto& meshes : mesh_spans)
        {
            ERHE_PROFILE_SCOPE("mesh span");
            ERHE_PROFILE_GPU_SCOPE(c_forward_renderer_render);

            m_primitive_buffers->update(meshes, visibility_filter);
            const auto draw_indirect_buffer_range = m_draw_indirect_buffers->update(meshes, primitive_mode, visibility_filter);
            if (draw_indirect_buffer_range.draw_indirect_count == 0)
            {
                continue;
            }
            m_primitive_buffers->bind();
            m_draw_indirect_buffers->bind();

            {
                ERHE_PROFILE_SCOPE("mdi");
                gl::multi_draw_elements_indirect(
                    pipeline.data.input_assembly.primitive_topology,
                    m_mesh_memory->gl_index_type(),
                    reinterpret_cast<const void *>(draw_indirect_buffer_range.range.first_byte_offset),
                    static_cast<GLsizei>(draw_indirect_buffer_range.draw_indirect_count),
                    static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
                );
            }
        }

        if (pass->end)
        {
            ERHE_PROFILE_SCOPE("pass end");
            pass->end();
        }
    }

    if (enable_shadows && erhe::graphics::Instance::info.use_bindless_texture)
    {
        ERHE_PROFILE_SCOPE("shadow texture non resident");
        gl::make_texture_handle_non_resident_arb(shadow_texture_handle);
    }
}

void Forward_renderer::render_fullscreen(
    const Render_parameters&  parameters,
    const erhe::scene::Light* light
)
{
    ERHE_PROFILE_FUNCTION

    const auto&    viewport              = parameters.viewport;
    const auto*    camera                = parameters.camera;
    const auto&    lights                = parameters.lights;
    const auto&    passes                = parameters.passes;
    const bool     enable_shadows        = m_shadow_renderer && (!lights.empty());
    const uint64_t shadow_texture_handle = enable_shadows
        ?
            erhe::graphics::get_handle(
                *m_shadow_renderer->texture(),
                *m_programs->nearest_sampler.get()
            )
        : 0;

    erhe::graphics::Scoped_debug_group forward_renderer_render{c_forward_renderer_render};

    gl::viewport(viewport.x, viewport.y, viewport.width, viewport.height);
    if (camera != nullptr)
    {
        m_camera_buffers->update(*camera->projection(), *camera, viewport, camera->get_exposure());
        m_camera_buffers->bind();
    }

    if (light != nullptr)
    {
        const auto* light_projection_transforms = parameters.light_projections.get_light_projection_transforms_for_light(light);
        if (light_projection_transforms != nullptr)
        {
            m_light_buffers->update_control(light_projection_transforms->index);
            m_light_buffers->bind_control_buffer();
        }
        else
        {
            log_render->warn("Light {} has no light projection transforms", light->name());
        }
    }
    if (!lights.empty())
    {
        m_light_buffers->update(lights, parameters.light_projections, parameters.ambient_light);
        m_light_buffers->bind_light_buffer();
    }

    if (erhe::graphics::Instance::info.use_bindless_texture)
    {
        gl::make_texture_handle_resident_arb(shadow_texture_handle);
    }
    else
    {
        gl::bind_texture_unit(m_programs->shadow_texture_unit, m_shadow_renderer->texture()->gl_name());
        gl::bind_sampler     (m_programs->shadow_texture_unit, m_programs->nearest_sampler->gl_name());
    }

    for (auto& pass : passes)
    {
        const auto& pipeline = pass->pipeline;
        if (!pipeline.data.shader_stages)
        {
            continue;
        }

        if (pass->begin)
        {
            pass->begin();
        }

        erhe::graphics::Scoped_debug_group pass_scope{pass->pipeline.data.name};

        m_pipeline_state_tracker->execute(pipeline);
        gl::draw_arrays(pipeline.data.input_assembly.primitive_topology, 0, 3);

        if (pass->end)
        {
            pass->end();
        }
    }

    if (erhe::graphics::Instance::info.use_bindless_texture)
    {
        gl::make_texture_handle_non_resident_arb(shadow_texture_handle);
    }
}

} // namespace editor
