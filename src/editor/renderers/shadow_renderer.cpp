// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "renderers/shadow_renderer.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"

#include "rendergraph/shadow_render_node.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/program_interface.hpp"
#include "renderers/programs.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "windows/debug_view_window.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/gl/draw_indirect.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/gpu_timer.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <sstream>

namespace editor
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

Shadow_renderer::Shadow_renderer()
    : Component{c_type_name}
{
}

Shadow_renderer::~Shadow_renderer() noexcept
{
}

void Shadow_renderer::declare_required_components()
{
    require<erhe::application::Gl_context_provider>();
    require<Editor_message_bus>();
    require<Program_interface >();
    require<Programs          >();
    m_configuration = require<erhe::application::Configuration>();
    m_render_graph  = require<erhe::application::Rendergraph>();
    m_mesh_memory   = require<Mesh_memory>();
}

static constexpr std::string_view c_shadow_renderer_initialize_component{"Shadow_renderer::initialize_component()"};

void Shadow_renderer::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const auto& config = m_configuration->shadow_renderer;

    if (!config.enabled)
    {
        log_render->info("Shadow renderer disabled due to erhe.ini setting");
        return;
    }
    else
    {
        log_render->info(
            "Shadow renderer using shadow map resolution {0}x{0}, max {1} lights",
            config.shadow_map_resolution,
            config.shadow_map_max_light_count
        );
    }

    const erhe::application::Scoped_gl_context gl_context{
        Component::get<erhe::application::Gl_context_provider>()
    };

    erhe::graphics::Scoped_debug_group debug_group{c_shadow_renderer_initialize_component};

    const auto& shader_resources = *get<Program_interface>()->shader_resources.get();
    m_light_buffers         = std::make_unique<Light_buffer        >(shader_resources.light_interface);
    m_draw_indirect_buffers = std::make_unique<Draw_indirect_buffer>(m_configuration->renderer.max_draw_count);
    m_primitive_buffers     = std::make_unique<Primitive_buffer    >(shader_resources.primitive_interface);

    ERHE_VERIFY(m_configuration->shadow_renderer.shadow_map_max_light_count <= m_configuration->renderer.max_light_count);

    m_vertex_input = std::make_unique<Vertex_input_state>(
        erhe::graphics::Vertex_input_state_data::make(
            shader_resources.attribute_mappings,
            m_mesh_memory->gl_vertex_format(),
            m_mesh_memory->gl_vertex_buffer.get(),
            m_mesh_memory->gl_index_buffer.get()
        )
    );

    m_pipeline.data = {
        .name           = "Shadow Renderer",
        .shader_stages  = get<Programs>()->depth.get(),
        .vertex_input   = m_vertex_input.get(),
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_none,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->graphics.reverse_depth),
        .color_blend    = Color_blend_state::color_writes_disabled
    };

    m_gpu_timer = std::make_unique<erhe::graphics::Gpu_timer>("Shadow_renderer");

    get<Editor_message_bus>()->add_receiver(
        [&](Editor_message& message)
        {
            on_message(message);
        }
    );
}

void Shadow_renderer::post_initialize()
{
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
}

void Shadow_renderer::on_message(Editor_message& message)
{
    using namespace erhe::toolkit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_graphics_settings))
    {
        handle_graphics_settings_changed();
    }
}

static constexpr std::string_view c_shadow_renderer_render{"Shadow_renderer::render()"};

auto Shadow_renderer::create_node_for_viewport(
    const std::shared_ptr<Scene_view>& scene_view
) -> std::shared_ptr<Shadow_render_node>
{
    const auto& config        = m_configuration->shadow_renderer;
    const int   resolution    = config.enabled ? config.shadow_map_resolution      : 1;
    const int   light_count   = config.enabled ? config.shadow_map_max_light_count : 1;
    const bool  reverse_depth = m_configuration->graphics.reverse_depth;

    auto shadow_render_node = std::make_shared<Shadow_render_node>(
        *this,
        scene_view,
        resolution,
        light_count,
        reverse_depth
    );
    m_render_graph->register_node(shadow_render_node);
    m_nodes.push_back(shadow_render_node);
    return shadow_render_node;
}

void Shadow_renderer::handle_graphics_settings_changed()
{
    const auto& config        = m_configuration->shadow_renderer;
    const int   resolution    = config.enabled ? config.shadow_map_resolution      : 1;
    const int   light_count   = config.enabled ? config.shadow_map_max_light_count : 1;
    const bool  reverse_depth = m_configuration->graphics.reverse_depth;

    for (const auto& node : m_nodes)
    {
        node->reconfigure(resolution, light_count, reverse_depth);
    }
}

auto Shadow_renderer::get_node_for_view(
    const Scene_view* scene_view
) -> std::shared_ptr<Shadow_render_node>
{
    if (scene_view == nullptr)
    {
        return {};
    }
    auto i = std::find_if(
        m_nodes.begin(),
        m_nodes.end(),
        [scene_view](const auto& entry)
        {
            return entry->get_scene_view().get() == scene_view;
        }
    );
    if (i == m_nodes.end())
    {
        return {};
    }
    return *i;
}

auto Shadow_renderer::get_nodes() const -> const std::vector<std::shared_ptr<Shadow_render_node>>&
{
    return m_nodes;
}

void Shadow_renderer::next_frame()
{
    const auto& config = m_configuration->shadow_renderer;
    if (!config.enabled)
    {
        return;
    }

    m_light_buffers        ->next_frame();
    m_draw_indirect_buffers->next_frame();
    m_primitive_buffers    ->next_frame();
}

auto Shadow_renderer::render(const Render_parameters& parameters) -> bool
{
    // Also assigns lights slot in uniform block shader resource
    parameters.light_projections = Light_projections{
        parameters.lights,
        parameters.view_camera,
        ////parameters.view_camera_viewport,
        parameters.light_camera_viewport,
        erhe::graphics::get_handle(
            parameters.texture,
            *get<Programs>()->nearest_sampler.get()
        )
    };

    if (
        !m_configuration->shadow_renderer.enabled ||
        !parameters.scene_root
    )
    {
        return false;
    }

    ERHE_PROFILE_FUNCTION

    ERHE_PROFILE_GPU_SCOPE(c_shadow_renderer_render)

    erhe::graphics::Scoped_debug_group debug_group{c_shadow_renderer_render};
    erhe::graphics::Scoped_gpu_timer   timer      {*m_gpu_timer.get()};

    const auto& mesh_spans = parameters.mesh_spans;
    const auto& lights     = parameters.lights;

    m_pipeline_state_tracker->execute(m_pipeline);
    gl::viewport(
        parameters.light_camera_viewport.x,
        parameters.light_camera_viewport.y,
        parameters.light_camera_viewport.width,
        parameters.light_camera_viewport.height
    );

    erhe::scene::Item_filter shadow_filter{
        .require_all_bits_set           =
            erhe::scene::Item_flags::visible |
            erhe::scene::Item_flags::shadow_cast,
        .require_at_least_one_bit_set   = 0u,
        .require_all_bits_clear         = 0u,
        .require_at_least_one_bit_clear = 0u
    };

    m_light_buffers->update(
        lights,
        &parameters.light_projections,
        glm::vec3{0.0f}
    );
    m_light_buffers->bind_light_buffer();

    for (const auto& meshes : mesh_spans)
    {
        m_primitive_buffers->update(meshes, shadow_filter);
        const auto draw_indirect_buffer_range = m_draw_indirect_buffers->update(
            meshes,
            erhe::primitive::Primitive_mode::polygon_fill,
            shadow_filter
        );
        if (draw_indirect_buffer_range.draw_indirect_count > 0)
        {
            m_primitive_buffers->bind();
            m_draw_indirect_buffers->bind();
        }

        for (const auto& light : lights)
        {
            if (!light->cast_shadow)
            {
                continue;
            }

            auto* light_projection_transform = parameters.light_projections.get_light_projection_transforms_for_light(light.get());
            if (light_projection_transform == nullptr)
            {
                //// log_render->warn("Light {} has no light projection transforms", light->name());
                continue;
            }
            const std::size_t light_index = light_projection_transform->index;
            if (light_index >= parameters.framebuffers.size())
            {
                continue;
            }

            //Frustum_tiler frustum_tiler{*m_texture.get()};
            //frustum_tiler.update(
            //    light_index,
            //    m_light_projections.projection_transforms[light_index].clip_from_world.matrix(),
            //    parameters.view_camera,
            //    parameters.view_camera_viewport
            //);

            {
                ERHE_PROFILE_SCOPE("bind fbo");
                gl::bind_framebuffer(
                    gl::Framebuffer_target::draw_framebuffer,
                    parameters.framebuffers[light_index]->gl_name()
                );
            }

            {
                static constexpr std::string_view c_id_clear{"clear"};

                ERHE_PROFILE_SCOPE("clear fbo");
                ERHE_PROFILE_GPU_SCOPE(c_id_clear);

                gl::clear_buffer_fv(gl::Buffer::depth, 0, m_configuration->depth_clear_value_pointer());
            }

            if (draw_indirect_buffer_range.draw_indirect_count == 0)
            {
                continue;
            }

            m_light_buffers->update_control(light_index);
            m_light_buffers->bind_control_buffer();

            {
                static constexpr std::string_view c_id_mdi{"mdi"};

                ERHE_PROFILE_SCOPE("mdi");
                ERHE_PROFILE_GPU_SCOPE(c_id_mdi);
                gl::multi_draw_elements_indirect(
                    m_pipeline.data.input_assembly.primitive_topology,
                    m_mesh_memory->gl_index_type(),
                    reinterpret_cast<const void *>(draw_indirect_buffer_range.range.first_byte_offset),
                    static_cast<GLsizei>(draw_indirect_buffer_range.draw_indirect_count),
                    static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
                );
            }
        }
    }
    return true;
}

} // namespace editor
