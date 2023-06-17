// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/renderer/shadow_renderer.hpp"

#include "erhe/renderer/renderer_log.hpp"
#include "erhe/renderer/program_interface.hpp"

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
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <sstream>

namespace erhe::renderer
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

Shadow_renderer* g_shadow_renderer{nullptr};

Shadow_renderer::Shadow_renderer()
    : Component{c_type_name}
{
}

Shadow_renderer::~Shadow_renderer() noexcept
{
}

void Shadow_renderer::deinitialize_component()
{
    ERHE_VERIFY(g_shadow_renderer == this);
    m_pipeline_cache_entries.clear();
    m_vertex_input.reset();
    m_gpu_timer.reset();
#if 0
    m_nodes.clear();
#endif
    m_light_buffers.reset();
    m_draw_indirect_buffers.reset();
    m_primitive_buffers.reset();
    g_shadow_renderer = nullptr;
}

void Shadow_renderer::declare_required_components()
{
    require<erhe::application::Gl_context_provider>();
    require<Program_interface>();
}

static constexpr std::string_view c_shadow_renderer_initialize_component{"Shadow_renderer::initialize_component()"};

void Shadow_renderer::initialize_component()
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(g_shadow_renderer == nullptr);

    auto ini = erhe::application::get_ini("erhe.ini", "shadow_renderer");
    ini->get("enabled",                    config.enabled);
    ini->get("tight_frustum_fit",          config.tight_frustum_fit);
    ini->get("shadow_map_resolution",      config.shadow_map_resolution);
    ini->get("shadow_map_max_light_count", config.shadow_map_max_light_count);

    if (!config.enabled) {
        log_render->info("Shadow renderer disabled due to erhe.ini setting");
        return;
    } else {
        log_render->info(
            "Shadow renderer using shadow map resolution {0}x{0}, max {1} lights",
            config.shadow_map_resolution,
            config.shadow_map_max_light_count
        );
    }

    const erhe::application::Scoped_gl_context gl_context;

    erhe::graphics::Scoped_debug_group debug_group{c_shadow_renderer_initialize_component};

    auto& shader_resources  = *g_program_interface->shader_resources.get();
    m_light_buffers         = std::make_unique<Light_buffer        >(&shader_resources.light_interface);
    m_draw_indirect_buffers = std::make_unique<Draw_indirect_buffer>(g_program_interface->config.max_draw_count);
    m_primitive_buffers     = std::make_unique<Primitive_buffer    >(&shader_resources.primitive_interface);

    ERHE_VERIFY(config.shadow_map_max_light_count <= g_program_interface->config.max_light_count);

    auto prototype = shader_resources.make_prototype(
        "res/shaders",
        erhe::graphics::Shader_stages::Create_info{
            .name           = "depth",
            .dump_interface = false
        }
    );

    prototype->compile_shaders();
    prototype->link_program();
    m_shader_stages = shader_resources.make_program(*prototype.get());

    m_pipeline_cache_entries.resize(8);

    m_gpu_timer = std::make_unique<erhe::graphics::Gpu_timer>("Shadow_renderer");

    m_nearest_sampler = std::make_unique<erhe::graphics::Sampler>(
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::nearest,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .debug_label = "Shadow_renderer"
        }
    );

    g_shadow_renderer = this;
}

auto Shadow_renderer::get_pipeline(
    const Vertex_input_state* vertex_input_state
) -> erhe::graphics::Pipeline&
{
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
    lru_entry->pipeline.data = {
        .name           = "Shadow Renderer",
        .shader_stages  = m_shader_stages.get(),
        .vertex_input   = vertex_input_state,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_none,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(erhe::application::g_configuration->graphics.reverse_depth),
        .color_blend    = Color_blend_state::color_writes_disabled
    };
    return lru_entry->pipeline;
}

static constexpr std::string_view c_shadow_renderer_render{"Shadow_renderer::render()"};

void Shadow_renderer::next_frame()
{
    if (!config.enabled) {
        return;
    }

    m_light_buffers        ->next_frame();
    m_draw_indirect_buffers->next_frame();
    m_primitive_buffers    ->next_frame();
}

auto Shadow_renderer::render(const Render_parameters& parameters) -> bool
{
    log_shadow_renderer->trace(
        "Making light projections using texture '{}' sampler '{}' handle '{}'",
        parameters.texture->debug_label(),
        m_nearest_sampler->debug_label(),
        erhe::graphics::format_texture_handle(parameters.light_projections.shadow_map_texture_handle)
    );
    const auto shadow_texture_handle =
        erhe::graphics::get_handle(
            *parameters.texture.get(),
            *m_nearest_sampler.get()
        );

    // Also assigns lights slot in uniform block shader resource
    parameters.light_projections = Light_projections{
        parameters.lights,
        parameters.view_camera,
        ////parameters.view_camera_viewport,
        parameters.light_camera_viewport,
        parameters.texture,
        shadow_texture_handle
    };

    if (!config.enabled) {
        return false;
    }

    ERHE_PROFILE_FUNCTION();

    ERHE_PROFILE_GPU_SCOPE(c_shadow_renderer_render)

    erhe::graphics::Scoped_debug_group debug_group{c_shadow_renderer_render};
    erhe::graphics::Scoped_gpu_timer   timer      {*m_gpu_timer.get()};

    const auto& mesh_spans = parameters.mesh_spans;
    const auto& lights     = parameters.lights;

    auto& pipeline = get_pipeline(parameters.vertex_input_state);

    erhe::graphics::g_opengl_state_tracker->execute(pipeline);
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

    const auto light_range = m_light_buffers->update(
        lights,
        &parameters.light_projections,
        glm::vec3{0.0f}
    );
    m_light_buffers->bind_light_buffer(light_range);

    log_shadow_renderer->trace("Rendering shadow map to '{}'", parameters.texture->debug_label());

    for (const auto& meshes : mesh_spans) {
        const auto primitive_range = m_primitive_buffers->update(meshes, shadow_filter, Primitive_interface_settings{});
        const auto draw_indirect_buffer_range = m_draw_indirect_buffers->update(
            meshes,
            erhe::primitive::Primitive_mode::polygon_fill,
            shadow_filter
        );
        if (draw_indirect_buffer_range.draw_indirect_count > 0) {
            m_primitive_buffers->bind(primitive_range);
            m_draw_indirect_buffers->bind(draw_indirect_buffer_range.range);
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
                ERHE_PROFILE_GPU_SCOPE(c_id_clear);

                gl::clear_buffer_fv(gl::Buffer::depth, 0, erhe::application::g_configuration->depth_clear_value_pointer());
            }

            if (draw_indirect_buffer_range.draw_indirect_count == 0) {
                continue;
            }

            const auto control_range = m_light_buffers->update_control(light_index);
            m_light_buffers->bind_control_buffer(control_range);

            {
                static constexpr std::string_view c_id_mdi{"mdi"};

                ERHE_PROFILE_SCOPE("mdi");
                ERHE_PROFILE_GPU_SCOPE(c_id_mdi);
                gl::multi_draw_elements_indirect(
                    pipeline.data.input_assembly.primitive_topology,
                    parameters.index_type,
                    reinterpret_cast<const void *>(draw_indirect_buffer_range.range.first_byte_offset),
                    static_cast<GLsizei>(draw_indirect_buffer_range.draw_indirect_count),
                    static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
                );
            }
        }
    }
    return true;
}

} // namespace erhe::renderer
