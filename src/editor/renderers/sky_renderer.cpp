#include "renderers/sky_renderer.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/sky_config.hpp"
#include "editor_log.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/input_assembly_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_graphics/state/viewport_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <filesystem>

namespace editor {

namespace {

constexpr int c_transmittance_width  = 256;
constexpr int c_transmittance_height = 64;
constexpr int c_multiscatter_size    = 32;

// Sampler binding points within the atmosphere bind group layout (user-facing
// values passed to set_sampled_image; the Vulkan backend offsets them past the
// camera UBO binding).
constexpr uint32_t c_s_transmittance = 0;
constexpr uint32_t c_s_multiscatter  = 1;

[[nodiscard]] auto shader_paths() -> std::vector<std::filesystem::path>
{
    return {
        std::filesystem::path{"res"} / std::filesystem::path{"shaders"},
        std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"}
    };
}

} // anonymous namespace

Sky_renderer::Sky_renderer(
    erhe::graphics::Device&                  graphics_device,
    erhe::graphics::Command_buffer&          init_command_buffer,
    App_context&                             context,
    erhe::scene_renderer::Program_interface& program_interface,
    const int                                view_count
)
    : m_context          {context}
    , m_graphics_device  {graphics_device}
    , m_program_interface{program_interface}
    // Use the (possibly clamped) camera UBO array size as the authoritative
    // view count so it always matches what Camera_buffer::update_views expects;
    // the passed view_count is the pre-clamp request (equal in practice).
    , m_view_count       {static_cast<int>(program_interface.camera_interface.view_count)}
{
    static_cast<void>(init_command_buffer);
    static_cast<void>(view_count);

#if defined(ERHE_GRAPHICS_API_VULKAN) || defined(ERHE_GRAPHICS_API_OPENGL)
    using namespace erhe::graphics;
    using erhe::utility::Debug_label;

    const std::filesystem::path editor_shaders = std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"};

    // Storage-image compute requires GL 4.3 (use_compute_shader). On GL < 4.3 or
    // when compute is force-disabled, leave all members null so
    // is_atmosphere_supported() returns false and the gradient sky is used.
    // (Vulkan always reports use_compute_shader == true.)
    if (!graphics_device.get_info().use_compute_shader) {
        return;
    }

    // LUT textures: written by compute (storage) then sampled by the
    // atmosphere fragment shader (sampled). R16G16B16A16F.
    m_transmittance_lut = std::make_unique<Texture>(
        graphics_device,
        Texture_create_info{
            .device      = graphics_device,
            .usage_mask  = Image_usage_flag_bit_mask::storage | Image_usage_flag_bit_mask::sampled,
            .type        = Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_16_vec4_float,
            .width       = c_transmittance_width,
            .height      = c_transmittance_height,
            .level_count = 1,
            .debug_label = Debug_label{"Sky transmittance LUT"}
        }
    );
    m_multiscatter_lut = std::make_unique<Texture>(
        graphics_device,
        Texture_create_info{
            .device      = graphics_device,
            .usage_mask  = Image_usage_flag_bit_mask::storage | Image_usage_flag_bit_mask::sampled,
            .type        = Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_16_vec4_float,
            .width       = c_multiscatter_size,
            .height      = c_multiscatter_size,
            .level_count = 1,
            .debug_label = Debug_label{"Sky multi-scatter LUT"}
        }
    );
    m_lut_sampler = std::make_unique<Sampler>(
        graphics_device,
        Sampler_create_info{
            .min_filter  = Filter::linear,
            .mag_filter  = Filter::linear,
            .mipmap_mode = Sampler_mipmap_mode::not_mipmapped,
            .debug_label = "Sky LUT"
        }
    );

    // Compute bind group layouts (storage images; no texture heap).
    m_transmittance_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point = 0,
                    .type          = Binding_type::storage_image,
                    .name          = "i_transmittance",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba16f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = "Sky transmittance compute",
            .uses_texture_heap = false
        }
    );
    m_multiscatter_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point = 0,
                    .type          = Binding_type::storage_image,
                    .name          = "i_transmittance",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba16f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = 1,
                    .type          = Binding_type::storage_image,
                    .name          = "i_multiscatter",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba16f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = "Sky multi-scatter compute",
            .uses_texture_heap = false
        }
    );

    // Atmosphere graphics bind group layout: camera UBO + 2 LUT samplers.
    const uint32_t camera_binding_point = m_program_interface.camera_interface.camera_block.get_binding_point();
    m_atmosphere_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point = camera_binding_point,
                    .type          = Binding_type::uniform_buffer,
                    .stage_flags   = Shader_stage_flags::vertex | Shader_stage_flags::fragment
                },
                {
                    .binding_point   = c_s_transmittance,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_transmittance",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::fragment
                },
                {
                    .binding_point   = c_s_multiscatter,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_multiscatter",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::fragment
                }
            },
            .debug_label       = "Sky atmosphere",
            .uses_texture_heap = false
        }
    );

    // Compute shaders. LUT dimensions are compile-time constants in the GLSL;
    // the storage images come from the bind group layout.
    m_transmittance_compute = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = "sky_transmittance_lut",
            .shaders             = { { Shader_type::compute_shader, editor_shaders / "sky_transmittance_lut.comp" } },
            .extra_include_paths = shader_paths(),
            .bind_group_layout   = m_transmittance_layout.get()
        }
    );
    m_multiscatter_compute = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = "sky_multiscatter_lut",
            .shaders             = { { Shader_type::compute_shader, editor_shaders / "sky_multiscatter_lut.comp" } },
            .extra_include_paths = shader_paths(),
            .bind_group_layout   = m_multiscatter_layout.get()
        }
    );

    // Atmosphere graphics shader (fullscreen pass; reads the camera UBO).
    static const Fragment_outputs s_fragment_outputs{
        Fragment_output{ .name = "out_color", .type = Glsl_type::float_vec4, .location = 0 }
    };
    m_atmosphere_shader = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = "sky_atmosphere",
            // The camera UBO block references the "Camera" struct type, so the
            // struct definition must be emitted before the block.
            .struct_types        = { &m_program_interface.camera_interface.camera_struct },
            .interface_blocks    = { &m_program_interface.camera_interface.camera_block },
            .fragment_outputs    = &s_fragment_outputs,
            .no_vertex_input     = true,
            .shaders             = {
                { Shader_type::vertex_shader,   editor_shaders / "sky_atmosphere.vert" },
                { Shader_type::fragment_shader, editor_shaders / "sky_atmosphere.frag" }
            },
            .extra_include_paths = shader_paths(),
            .bind_group_layout   = m_atmosphere_layout.get(),
            .view_count          = static_cast<uint32_t>(m_view_count)
        }
    );

    graphics_device.get_shader_monitor().add(*m_transmittance_compute);
    graphics_device.get_shader_monitor().add(*m_multiscatter_compute);
    graphics_device.get_shader_monitor().add(*m_atmosphere_shader);

    m_transmittance_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "sky_transmittance_lut",
            .shader_stages     = &m_transmittance_compute->shader_stages,
            .bind_group_layout = m_transmittance_layout.get()
        }
    );
    m_multiscatter_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "sky_multiscatter_lut",
            .shader_stages     = &m_multiscatter_compute->shader_stages,
            .bind_group_layout = m_multiscatter_layout.get()
        }
    );

    // Atmosphere render pipeline. Mirrors the gradient sky pipeline: fullscreen
    // triangle at the far plane, depth test (equal) but no depth write, stencil
    // == 0 so it does not overdraw the selection silhouette.
    const bool reverse_depth = graphics_device.get_reverse_depth();
    m_atmosphere_pipeline = std::make_unique<Base_render_pipeline>(
        graphics_device,
        Base_render_pipeline_create_info{
            .debug_label          = Debug_label{"Sky atmosphere"},
            .input_assembly       = Input_assembly_state::triangle,
            .viewport_depth_range = Viewport_depth_range_state{
                .min_depth = reverse_depth ? 0.0f : 1.0f,
                .max_depth = reverse_depth ? 0.0f : 1.0f
            },
            .rasterization = Rasterization_state::cull_mode_none,
            .depth_stencil = Depth_stencil_state{
                .depth_test_enable   = true,
                .depth_write_enable  = false,
                .depth_compare_op    = Compare_operation::equal,
                .stencil_test_enable = true,
                .stencil_front = {
                    .stencil_fail_op = Stencil_op::keep,
                    .z_fail_op       = Stencil_op::keep,
                    .z_pass_op       = Stencil_op::keep,
                    .function        = Compare_operation::equal,
                    .reference       = 0u,
                    .test_mask       = 0b11111111u,
                    .write_mask      = 0b00000000u
                },
                .stencil_back = {
                    .stencil_fail_op = Stencil_op::keep,
                    .z_fail_op       = Stencil_op::keep,
                    .z_pass_op       = Stencil_op::keep,
                    .function        = Compare_operation::equal,
                    .reference       = 0u,
                    .test_mask       = 0b11111111u,
                    .write_mask      = 0b00000000u
                }
            }
        }
    );

    m_camera_buffer = std::make_unique<erhe::scene_renderer::Camera_buffer>(
        graphics_device,
        m_program_interface.camera_interface
    );
#endif
}

Sky_renderer::~Sky_renderer() noexcept = default;

auto Sky_renderer::is_atmosphere_supported() const -> bool
{
    return m_atmosphere_pipeline != nullptr;
}

void Sky_renderer::ensure_luts(erhe::graphics::Device& graphics_device, erhe::graphics::Command_buffer& command_buffer)
{
    if (!is_atmosphere_supported() || m_luts_ready) {
        return;
    }

#if defined(ERHE_GRAPHICS_API_VULKAN) || defined(ERHE_GRAPHICS_API_OPENGL)
    using namespace erhe::graphics;

    log_render->info("Sky_renderer::ensure_luts: generating atmosphere LUTs");
    erhe::graphics::Scoped_debug_group lut_scope{command_buffer, "Sky LUT generation"};

    // Pass 1: transmittance LUT (writes the storage image).
    command_buffer.transition_texture_layout(*m_transmittance_lut, Image_layout::general);
    {
        Compute_command_encoder encoder = graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_transmittance_layout.get());
        encoder.set_compute_pipeline(*m_transmittance_pipeline);
        encoder.set_storage_image(0, *m_transmittance_lut);
        encoder.dispatch_compute(c_transmittance_width / 8, c_transmittance_height / 8, 1);
    }

    // Make the transmittance writes visible to the multi-scatter pass (both
    // stay in GENERAL, so transition_texture_layout would be a no-op).
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);

    // Pass 2: multi-scatter LUT (reads transmittance via imageLoad, writes
    // multi-scatter).
    command_buffer.transition_texture_layout(*m_multiscatter_lut, Image_layout::general);
    {
        Compute_command_encoder encoder = graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_multiscatter_layout.get());
        encoder.set_compute_pipeline(*m_multiscatter_pipeline);
        encoder.set_storage_image(0, *m_transmittance_lut);
        encoder.set_storage_image(1, *m_multiscatter_lut);
        encoder.dispatch_compute(c_multiscatter_size / 8, c_multiscatter_size / 8, 1);
    }

    // Both LUTs become fragment-sampled textures for the atmosphere pass.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_transmittance_lut, Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_multiscatter_lut,  Image_layout::shader_read_only_optimal);

    m_luts_ready = true;
#else
    static_cast<void>(graphics_device);
    static_cast<void>(command_buffer);
#endif
}

void Sky_renderer::render_atmosphere(const Render_context& context)
{
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        log_render->info(
            "Sky_renderer::render_atmosphere first call: supported={} luts_ready={} encoder={} render_pass={} views={}",
            is_atmosphere_supported(), m_luts_ready,
            (context.encoder != nullptr), (context.render_pass != nullptr),
            context.views.size()
        );
    }

    if (!is_atmosphere_supported() || !m_luts_ready) {
        return;
    }

#if defined(ERHE_GRAPHICS_API_VULKAN) || defined(ERHE_GRAPHICS_API_OPENGL)
    using namespace erhe::graphics;

    if ((context.encoder == nullptr) || (context.render_pass == nullptr) || context.views.empty()) {
        return;
    }

    // Sun direction + atmosphere parameters from config, overridden by the
    // scene's first directional light when present.
    const Sky_config& sky_config = m_context.editor_settings->sky;

    const float elevation = glm::radians(sky_config.sun_elevation_deg);
    const float azimuth   = glm::radians(sky_config.sun_azimuth_deg);
    const float cos_el    = std::cos(elevation);
    glm::vec3   toward_sun{cos_el * std::cos(azimuth), std::sin(elevation), cos_el * std::sin(azimuth)};

    const std::shared_ptr<Scene_root> scene_root = context.scene_view.get_scene_root();
    if (scene_root) {
        for (const std::shared_ptr<erhe::scene::Light>& light : scene_root->layers().light()->lights) {
            if (!light || (light->type != erhe::scene::Light_type::directional)) {
                continue;
            }
            const erhe::scene::Node* node = light->get_node();
            if (node == nullptr) {
                continue;
            }
            // erhe stores a directional light's direction as world_from_node * +Z,
            // which is the direction from the surface toward the light (see
            // standard.frag: L = normalize(light.direction_and_outer_spot_cos.xyz),
            // used directly in dot(N, L)). That is already the toward-sun direction
            // the atmosphere wants, so use it as-is - negating it would put the sun
            // below the horizon and render a black night sky.
            const glm::vec3 to_light  = glm::vec3{node->world_from_node() * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}};
            const float     length_sq = glm::dot(to_light, to_light);
            if (length_sq > 1e-8f) {
                toward_sun = glm::normalize(to_light);
            }
            break;
        }
    }

    const float cos_sun_radius = std::cos(glm::radians(sky_config.sun_angular_radius_deg));

    erhe::scene_renderer::Sky_parameters sky_parameters{};
    sky_parameters.sun_direction = glm::vec4{toward_sun, sky_config.sun_intensity};
    sky_parameters.atmosphere    = glm::vec4{
        static_cast<float>(sky_config.march_steps),
        sky_config.observer_altitude_km,
        cos_sun_radius,
        sky_config.sun_disc_intensity
    };

    const float exposure = (context.camera != nullptr) ? context.camera->get_exposure() : 1.0f;

    // Camera_buffer::update_views requires exactly view_count entries; pad with
    // the first view (capacity retained -> no steady-state allocation).
    m_view_scratch.clear();
    for (const erhe::scene_renderer::Camera_view_input& view : context.views) {
        if (m_view_scratch.size() >= static_cast<std::size_t>(m_view_count)) {
            break;
        }
        m_view_scratch.push_back(view);
    }
    while (m_view_scratch.size() < static_cast<std::size_t>(m_view_count)) {
        m_view_scratch.push_back(m_view_scratch.front());
    }

    // Fetch the pipeline before acquiring the ring-buffer range, so an early
    // return (pipeline not ready) does not leave an unreleased range.
    const erhe::scene_renderer::Vertex_input_entry& empty_vertex_input = m_context.mesh_memory->get_empty_vertex_input();

    Render_pipeline* pipeline = m_atmosphere_pipeline->get_pipeline_for(
        context.render_pass->get_descriptor(),
        nullptr,
        &m_atmosphere_shader->shader_stages,
        empty_vertex_input.vertex_input.get(),
        &empty_vertex_input.vertex_format
    );
    if (pipeline == nullptr) {
        log_render->warn("Sky_renderer::render_atmosphere: no render pipeline (atmosphere shader not ready)");
        return;
    }

    erhe::graphics::Scoped_debug_group atmosphere_scope{*context.command_buffer, "Sky atmosphere"};

    erhe::graphics::Ring_buffer_range camera_range = m_camera_buffer->update_views(
        m_view_scratch,
        exposure,
        erhe::scene_renderer::Grid_parameters{},
        sky_parameters,
        0,
        context.scene_view.get_reverse_depth(),
        context.scene_view.get_depth_range(),
        context.scene_view.get_conventions()
    );

    Render_command_encoder& encoder = *context.encoder;
    encoder.set_render_pipeline(*pipeline);
    encoder.set_bind_group_layout(m_atmosphere_layout.get());
    m_camera_buffer->bind(encoder, camera_range);
    encoder.set_sampled_image(c_s_transmittance, *m_transmittance_lut, *m_lut_sampler);
    encoder.set_sampled_image(c_s_multiscatter,  *m_multiscatter_lut,  *m_lut_sampler);
    encoder.set_viewport_rect(context.viewport.x, context.viewport.y, context.viewport.width, context.viewport.height);
    encoder.set_scissor_rect (context.viewport.x, context.viewport.y, context.viewport.width, context.viewport.height);
    encoder.draw_primitives(Primitive_type::triangle, 0, 3);

    // update_views() already closed the range; release it so the ring buffer
    // tracks its GPU lifetime (and the dtor's released/cancelled invariant holds).
    camera_range.release();
#else
    static_cast<void>(context);
#endif
}

} // namespace editor
