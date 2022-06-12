#include "renderers/shadow_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/program_interface.hpp"
#include "renderers/programs.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace editor
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

constexpr size_t s_max_light_count    = 4;
constexpr size_t s_texture_resolution = 1 * 1024;
constexpr bool   s_enable             = true;

Shadow_renderer::Shadow_renderer()
    : Component    {c_label}
    , Base_renderer{std::string{c_label}}
{
}

Shadow_renderer::~Shadow_renderer() = default;

void Shadow_renderer::connect()
{
    base_connect(this);

    require<erhe::application::Gl_context_provider>();
    require<Program_interface>();
    require<Programs>();

    m_mesh_memory            = require<Mesh_memory>();
    m_configuration          = require<erhe::application::Configuration>();
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
}

static constexpr std::string_view c_shadow_renderer_initialize_component{"Shadow_renderer::initialize_component()"};

void Shadow_renderer::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const erhe::application::Scoped_gl_context gl_context{
        Component::get<erhe::application::Gl_context_provider>()
    };

    erhe::graphics::Scoped_debug_group debug_group{c_shadow_renderer_initialize_component};

    create_frame_resources(1, 256, 256, 1000, 1000);

    const auto& shader_resources = *get<Program_interface>()->shader_resources.get();
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
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
        .color_blend    = Color_blend_state::color_writes_disabled
    };

    {
        ERHE_PROFILE_SCOPE("allocating shadow map array texture");

        m_texture = std::make_unique<Texture>(
            Texture::Create_info
            {
                .target          = gl::Texture_target::texture_2d_array,
                .internal_format = gl::Internal_format::depth_component32f,
                .width           = s_enable ? s_texture_resolution : 1,
                .height          = s_enable ? s_texture_resolution : 1,
                .depth           = s_max_light_count,
            }
        );
        m_texture->set_debug_label("Shadowmaps");
        //float depth_clear_value = erhe::graphics::Instance::depth_clear_value;
        //gl::clear_tex_image(m_texture->gl_name(), 0, gl::Pixel_format::depth_component, gl::Pixel_type::float_, &depth_clear_value);
    }

    for (size_t i = 0; i < s_max_light_count; ++i)
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
        .reverse_depth = m_configuration->reverse_depth
    };

    m_gpu_timer = std::make_unique<erhe::graphics::Gpu_timer>("Shadow_renderer");
}

static constexpr std::string_view c_shadow_renderer_render{"Shadow_renderer::render()"};

void Shadow_renderer::render(const Render_parameters& parameters)
{
    if constexpr (!s_enable)
    {
        return;
    }

    ERHE_PROFILE_FUNCTION

    ERHE_PROFILE_GPU_SCOPE(c_shadow_renderer_render)

    erhe::graphics::Scoped_debug_group debug_group{c_shadow_renderer_render};
    erhe::graphics::Scoped_gpu_timer   timer      {*m_gpu_timer.get()};

    const auto& mesh_spans = parameters.mesh_spans;
    const auto& lights     = parameters.lights;

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

    update_light_buffer(
        lights,
        glm::vec3{0.0f},
        m_viewport,
        erhe::graphics::get_handle(
            *m_texture.get(),
            *get<Programs>()->nearest_sampler.get()
        )
    );
    for (const auto& meshes : mesh_spans)
    {
        update_primitive_buffer(meshes, shadow_filter);
        const auto draw_indirect_buffer_range = update_draw_indirect_buffer(
            meshes,
            erhe::primitive::Primitive_mode::polygon_fill,
            shadow_filter
        );
        if (draw_indirect_buffer_range.draw_indirect_count == 0)
        {
            continue;
        }

        bind_light_buffer();
        bind_primitive_buffer();
        bind_draw_indirect_buffer();

        size_t light_index = 0;
        for (const auto& light : lights)
        {
            if (light_index >= s_max_light_count)
            {
                break; // TODO
            }

            if (!light->cast_shadow)
            {
                continue;
            }

            // if (light_index == m_slot)
            {
                update_camera_buffer(*light, m_viewport);

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

                bind_camera_buffer();

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
            ++light_index;
        }
    }

    ++m_slot;
    if (m_slot >= lights.size())
    {
        m_slot = 0;
    }
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
