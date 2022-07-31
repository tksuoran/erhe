// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "renderers/shadow_renderer.hpp"

#include "editor_log.hpp"

#include "renderers/mesh_memory.hpp"
#include "renderers/program_interface.hpp"
#include "renderers/programs.hpp"
#include "scene/scene_root.hpp"
#include "windows/debug_view_window.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/gl/draw_indirect.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/gpu_timer.hpp"
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

Shadow_render_node::Shadow_render_node(
    Shadow_renderer&                        shadow_renderer,
    const std::shared_ptr<Viewport_window>& viewport_window
)
    : erhe::application::Render_graph_node{
        fmt::format("Shadow render {}", viewport_window->name())
    }
    , m_shadow_renderer{shadow_renderer}
    , m_viewport_window{viewport_window}
{
}

void Shadow_render_node::execute_render_graph_node()
{
    // Render shadow maps
    auto*       scene_root = m_viewport_window->scene_root();
    const auto& layers     = scene_root->layers();
    if (scene_root->layers().content()->meshes.empty())
    {
        return;
    }

    scene_root->sort_lights();

    const erhe::scene::Camera* camera = m_viewport_window->camera();
    m_shadow_renderer.render(
        Shadow_renderer::Render_parameters{
            .scene_root           = scene_root,
            .view_camera          = camera,
            .view_camera_viewport = m_viewport_window->viewport(),
            .mesh_spans           = { layers.content()->meshes },
            .lights               = layers.light()->lights
        }
    );
}

Shadow_renderer::Shadow_renderer()
    : Component{c_label}
{
}

Shadow_renderer::~Shadow_renderer() noexcept
{
}

void Shadow_renderer::declare_required_components()
{
    require<erhe::application::Gl_context_provider>();
    require<Program_interface>();
    require<Programs>();

    m_mesh_memory   = require<Mesh_memory>();
    m_configuration = require<erhe::application::Configuration>();
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

    {
        ERHE_PROFILE_SCOPE("allocating shadow map array texture");

        m_texture = std::make_unique<Texture>(
            Texture::Create_info
            {
                .target          = gl::Texture_target::texture_2d_array,
                .internal_format = gl::Internal_format::depth_component32f,
                //.sparse          = erhe::graphics::Instance::info.use_sparse_texture,
                .width           = config.enabled ? config.shadow_map_resolution : 1,
                .height          = config.enabled ? config.shadow_map_resolution : 1,
                .depth           = config.shadow_map_max_light_count,
            }
        );

#if 0
        if (erhe::graphics::Instance::info.use_sparse_texture)
        {
            // commit the whole texture for now
            gl::texture_page_commitment_ext(
                m_texture->gl_name(),
                0,                  // level
                0,                  // x offset
                0,                  // y offset,
                0,                  // z offset
                m_texture->width(),
                m_texture->height(),
                m_texture->depth(),
                GL_TRUE
            );
        }
#endif

        m_texture->set_debug_label("Shadowmaps");
        //float depth_clear_value = erhe::graphics::Instance::depth_clear_value;
        //gl::clear_tex_image(m_texture->gl_name(), 0, gl::Pixel_format::depth_component, gl::Pixel_type::float_, &depth_clear_value);
    }

    for (int i = 0; i < config.shadow_map_max_light_count; ++i)
    {
        ERHE_PROFILE_SCOPE("framebuffer creation");

        Framebuffer::Create_info create_info;
        create_info.attach(gl::Framebuffer_attachment::depth_attachment, m_texture.get(), 0, static_cast<unsigned int>(i));
        auto framebuffer = std::make_unique<Framebuffer>(create_info);
        framebuffer->set_debug_label(fmt::format("Shadow {}", i));
        m_framebuffers.emplace_back(std::move(framebuffer));
    }

    m_viewport = {
        .x             = 0,
        .y             = 0,
        .width         = m_texture->width(),
        .height        = m_texture->height(),
        .reverse_depth = m_configuration->graphics.reverse_depth
    };

    m_gpu_timer = std::make_unique<erhe::graphics::Gpu_timer>("Shadow_renderer");
}

void Shadow_renderer::post_initialize()
{
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
}

static constexpr std::string_view c_shadow_renderer_render{"Shadow_renderer::render()"};

void Shadow_renderer::next_frame()
{
    m_light_buffers        ->next_frame();
    m_draw_indirect_buffers->next_frame();
    m_primitive_buffers    ->next_frame();
}

auto Shadow_renderer::light_projections() const -> const Light_projections&
{
    return m_light_projections;
}

[[nodiscard]] auto Shadow_renderer::scene_root() const -> Scene_root*
{
    return m_shadow_scene;
}

auto Shadow_renderer::render(const Render_parameters& parameters) -> const Light_projections&
{
    if (
        !m_configuration->shadow_renderer.enabled ||
        !parameters.scene_root
    )
    {
        return m_light_projections;
    }

    ERHE_PROFILE_FUNCTION

    ERHE_PROFILE_GPU_SCOPE(c_shadow_renderer_render)

    erhe::graphics::Scoped_debug_group debug_group{c_shadow_renderer_render};
    erhe::graphics::Scoped_gpu_timer   timer      {*m_gpu_timer.get()};

    const auto& mesh_spans = parameters.mesh_spans;
    const auto& lights     = parameters.lights;

    m_shadow_scene = parameters.scene_root;

    m_pipeline_state_tracker->execute(m_pipeline);
    gl::viewport(m_viewport.x, m_viewport.y, m_viewport.width, m_viewport.height);

    erhe::scene::Visibility_filter shadow_filter{
        .require_all_bits_set           =
            erhe::scene::Node_visibility::visible |
            erhe::scene::Node_visibility::shadow_cast,
        .require_at_least_one_bit_set   = 0u,
        .require_all_bits_clear         = 0u,
        .require_at_least_one_bit_clear = 0u
    };

    // Also assigns lights slot in uniform block shader resource
    m_light_projections = Light_projections{
        lights,
        parameters.view_camera,
        parameters.view_camera_viewport,
        m_viewport,
        erhe::graphics::get_handle(
            *m_texture.get(),
            *get<Programs>()->nearest_sampler.get()
        )
    };

    m_light_buffers->update(
        lights,
        m_light_projections,
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
        if (draw_indirect_buffer_range.draw_indirect_count == 0)
        {
            continue;
        }

        m_primitive_buffers->bind();
        m_draw_indirect_buffers->bind();

        for (const auto& light : lights)
        {
            if (!light->cast_shadow)
            {
                continue;
            }

            auto* light_projection_transform = m_light_projections.get_light_projection_transforms_for_light(light.get());
            if (light_projection_transform == nullptr)
            {
                log_render->warn("Light {} has no light projection transforms", light->name());
                continue;
            }
            const std::size_t light_index = light_projection_transform->index;
            m_light_buffers->update_control(light_index);
            m_light_buffers->bind_control_buffer();

            //Frustum_tiler frustum_tiler{*m_texture.get()};
            //frustum_tiler.update(
            //    light_index,
            //    m_light_projections.projection_transforms[light_index].clip_from_world.matrix(),
            //    parameters.view_camera,
            //    parameters.view_camera_viewport
            //);

            {
                ERHE_PROFILE_SCOPE("bind fbo");
                gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffers[light_index]->gl_name());
            }

            {
                static constexpr std::string_view c_id_clear{"clear"};

                ERHE_PROFILE_SCOPE("clear fbo");
                ERHE_PROFILE_GPU_SCOPE(c_id_clear);

                gl::clear_buffer_fv(gl::Buffer::depth, 0, m_configuration->depth_clear_value_pointer());
            }

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
    return m_light_projections;
}

auto Shadow_renderer::texture() const -> erhe::graphics::Texture*
{
    return m_texture.get();
}

auto Shadow_renderer::viewport() const -> erhe::scene::Viewport
{
    return m_viewport;
}

} // namespace editor
