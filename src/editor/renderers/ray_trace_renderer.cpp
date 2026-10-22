#include "renderers/ray_trace_renderer.hpp"
#include "renderers/ray_trace_scene.hpp"

#include "app_context.hpp"
#include "config/generated/ray_trace_config.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "renderers/render_settings.hpp"
#include "renderers/render_texture_reference.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>

namespace editor {

namespace {

// Raw binding points in this renderer's dedicated bind group layout. 2 and
// 3 are free here (light_control / primitive slots of the shared raster
// layout, which this layout does not use).
constexpr unsigned int c_control_binding_point         = 2;
constexpr unsigned int c_instance_record_binding_point = 3;

// Compile-time upper bound for transmissive bounces; sizes the shader's
// branching stack. The runtime cap (Ray_trace_config::max_bounces) is
// clamped to this.
constexpr int c_max_bounces = 12;

[[nodiscard]] auto shader_paths() -> std::vector<std::filesystem::path>
{
    return {
        std::filesystem::path{"res"} / std::filesystem::path{"shaders"},
        std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"}
    };
}

} // anonymous namespace

Ray_trace_renderer::Ray_trace_renderer(
    erhe::graphics::Device&                  graphics_device,
    erhe::graphics::Command_buffer&          init_command_buffer,
    App_context&                             context,
    erhe::scene_renderer::Program_interface& program_interface,
    erhe::scene_renderer::Mesh_memory&       mesh_memory
)
    : m_graphics_device{graphics_device}
    , m_context        {context}
    , m_control_block{
        graphics_device,
        "ray_trace_control",
        static_cast<int>(c_control_binding_point),
        erhe::graphics::Shader_resource::Type::uniform_block
    }
{
    using namespace erhe::graphics;
    using erhe::utility::Debug_label;

    m_rendered_outputs.resize(graphics_device.get_number_of_frames_in_flight());
    // Resolved by the render thread when it draws the Ray Trace window.
    m_output_texture_reference = std::make_shared<Render_texture_reference>(
        [this]() -> const erhe::graphics::Texture* {
            return m_output_texture.get();
        }
    );

    // The compute shader fetches the committed triangle's positions for the
    // geometric normal: with position fetch straight from the acceleration
    // structure, otherwise (Metal; SPIRV-Cross has no MSL lowering for
    // SPV_KHR_ray_tracing_position_fetch) from the stream-0 pool via the
    // per-instance device addresses.
    if (!graphics_device.get_info().use_ray_query) {
        log_startup->info("Ray_trace_renderer: ray query not available, GPU ray tracing disabled");
        return;
    }
    const bool use_position_fetch = graphics_device.get_info().use_ray_tracing_position_fetch;

    const std::filesystem::path editor_shaders = std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"};

    // Acceleration structures + per-instance records (shared with the other
    // ray-query consumers).
    m_scene_tlas = std::make_unique<Scene_tlas>(
        graphics_device,
        c_instance_record_binding_point,
        "Ray_trace_renderer"
    );

    // Runtime knobs (Ray_trace_config via the Ray Trace window / editor
    // settings): traced-ray budget and bounce cap. Padded to 16 bytes.
    m_control_max_rays_offset    = m_control_block.add_uint("max_rays"   )->get_offset_in_parent();
    m_control_max_bounces_offset = m_control_block.add_uint("max_bounces")->get_offset_in_parent();
    m_control_block.add_uint("control_reserved_0");
    m_control_block.add_uint("control_reserved_1");

    // Stream-1 attribute offsets (in uints) for the shader's manual vertex
    // fetch, derived from the Mesh_memory vertex format so they stay in
    // sync. Stream 1 is identical for the skinned and non-skinned formats.
    const erhe::dataformat::Vertex_format&   vertex_format = mesh_memory.vertex_format_not_skinned;
    const erhe::dataformat::Attribute_stream normal        = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::normal,    erhe::dataformat::normal_attribute);
    const erhe::dataformat::Attribute_stream tangent       = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::tangent,   0);
    const erhe::dataformat::Attribute_stream texcoord0     = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::tex_coord, 0);
    const erhe::dataformat::Attribute_stream color0        = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::color,     0);
    ERHE_VERIFY((normal   .attribute != nullptr) && (normal   .stream != nullptr));
    ERHE_VERIFY((tangent  .attribute != nullptr) && (tangent  .stream != nullptr));
    ERHE_VERIFY((texcoord0.attribute != nullptr) && (texcoord0.stream != nullptr));
    ERHE_VERIFY((color0   .attribute != nullptr) && (color0   .stream != nullptr));
    ERHE_VERIFY((normal   .attribute->offset % 4) == 0);
    ERHE_VERIFY((tangent  .attribute->offset % 4) == 0);
    ERHE_VERIFY((texcoord0.attribute->offset % 4) == 0);
    ERHE_VERIFY((color0   .attribute->offset % 4) == 0);

    // The output texture is created lazily in render() at the viewport's
    // size (and recreated on viewport resize), so the traced image has the
    // same resolution as the raster view.

    // Bindings share descriptor set 0: material / light / instance-record
    // blocks at their interface-declared binding points, the camera UBO at
    // its interface-declared binding point, then the top level acceleration
    // structure and the output storage image at the next raw binding points.
    // The texture heap (material textures) occupies set 1.
    auto to_binding_type = [](const Shader_resource& block) -> Binding_type {
        return (block.get_type() == Shader_resource::Type::shader_storage_block)
            ? Binding_type::storage_buffer
            : Binding_type::uniform_buffer;
    };
    const uint32_t camera_binding_point = program_interface.camera_interface.camera_block.get_binding_point();
    m_tlas_binding_point   = camera_binding_point + 1;
    m_output_binding_point = camera_binding_point + 2;
    m_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point = material_buffer_binding_point,
                    .type          = to_binding_type(program_interface.material_interface.material_block),
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = light_buffer_binding_point,
                    .type          = to_binding_type(program_interface.light_interface.light_block),
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = light_array_buffer_binding_point,
                    .type          = Binding_type::storage_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = light_cluster_buffer_binding_point,
                    .type          = Binding_type::storage_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                // The shadow record bindings exist only because bind_light_buffer() binds
                // them; the ray-hit code reads the 48-byte shading records (binding 9).
                {
                    .binding_point = light_shadowed_array_buffer_binding_point,
                    .type          = Binding_type::storage_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = light_shadow_world_array_buffer_binding_point,
                    .type          = Binding_type::storage_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_control_binding_point,
                    .type          = Binding_type::uniform_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = c_instance_record_binding_point,
                    .type          = Binding_type::storage_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = camera_binding_point,
                    .type          = Binding_type::uniform_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = m_tlas_binding_point,
                    .type          = Binding_type::acceleration_structure,
                    .name          = "s_tlas",
                    .stage_flags   = Shader_stage_flags::compute
                },
                {
                    .binding_point = m_output_binding_point,
                    .type          = Binding_type::storage_image,
                    .name          = "i_out",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba8",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = "Ray trace compute",
            .uses_texture_heap = true
        }
    );

    m_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name                = "ray_trace",
            .defines             = {
                { "ERHE_TLAS_BINDING",            fmt::format("{}", m_tlas_binding_point) },
                { "ERHE_RT_NORMAL_OFFSET",        fmt::format("{}", normal   .attribute->offset / 4) },
                { "ERHE_RT_TANGENT_OFFSET",       fmt::format("{}", tangent  .attribute->offset / 4) },
                { "ERHE_RT_TEXCOORD0_OFFSET",     fmt::format("{}", texcoord0.attribute->offset / 4) },
                { "ERHE_RT_COLOR0_OFFSET",        fmt::format("{}", color0   .attribute->offset / 4) },
                { "ERHE_RT_MAX_BOUNCES",          fmt::format("{}", c_max_bounces) },
                { "ERHE_RT_HAS_POSITION_FETCH",   use_position_fetch ? "1" : "0" }
            },
            .extensions          = [&]() {
                std::vector<Shader_stage_extension> extensions{
                    { Shader_type::compute_shader, "GL_EXT_ray_query" },
                    { Shader_type::compute_shader, "GL_EXT_buffer_reference" },
                    { Shader_type::compute_shader, "GL_EXT_buffer_reference_uvec2" }
                };
                if (use_position_fetch) {
                    extensions.push_back({ Shader_type::compute_shader, "GL_EXT_ray_tracing_position_fetch" });
                }
                return extensions;
            }(),
            .struct_types        = {
                &program_interface.camera_interface.camera_struct,
                &program_interface.material_interface.material_struct,
                &program_interface.light_interface.light_struct,
                &m_scene_tlas->get_instance_struct()
            },
            .interface_blocks    = {
                &program_interface.camera_interface.camera_block,
                &program_interface.material_interface.material_block,
                &program_interface.light_interface.light_block,
                &program_interface.light_interface.light_array_block,
                &m_control_block,
                &m_scene_tlas->get_instance_block()
            },
            .shaders             = { { Shader_type::compute_shader, editor_shaders / "ray_trace.comp" } },
            .extra_include_paths = shader_paths(),
            .bind_group_layout   = m_bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*m_shader_stages);

    m_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "ray_trace",
            .shader_stages     = &m_shader_stages->shader_stages,
            .bind_group_layout = m_bind_group_layout.get()
        }
    );

    m_camera_buffer = std::make_unique<erhe::scene_renderer::Camera_buffer>(
        graphics_device,
        program_interface.camera_interface
    );
    m_light_buffer = std::make_unique<erhe::scene_renderer::Light_buffer>(
        graphics_device,
        init_command_buffer,
        program_interface.light_interface
    );
    m_control_buffer = std::make_unique<Ring_buffer_client>(
        graphics_device,
        Buffer_target::uniform,
        "Ray_trace_renderer::control",
        c_control_binding_point
    );

    log_startup->info("Ray_trace_renderer: GPU ray tracing (ray query) available");
}

Ray_trace_renderer::~Ray_trace_renderer() noexcept = default;

auto Ray_trace_renderer::get_config() const -> const Ray_trace_config&
{
    return m_context.render_settings->ray_trace;
}

auto Ray_trace_renderer::is_supported() const -> bool
{
    return m_pipeline != nullptr;
}

auto Ray_trace_renderer::is_enabled() const -> bool
{
    erhe::utility::Thread_roles::assert_main_thread();
    return m_enabled;
}

void Ray_trace_renderer::set_enabled(const bool enabled)
{
    erhe::utility::Thread_roles::assert_main_thread();
    m_enabled = enabled;
}

void Ray_trace_renderer::set_render_enabled(const bool enabled)
{
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);
    m_render_enabled = enabled;
}

auto Ray_trace_renderer::get_output_texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    erhe::utility::Thread_roles::assert_render_thread();
    return m_output_texture;
}

auto Ray_trace_renderer::get_output_texture_reference() const -> const std::shared_ptr<erhe::graphics::Texture_reference>&
{
    return m_output_texture_reference;
}

auto Ray_trace_renderer::get_rendered_output(const std::optional<uint64_t> frame_index) const -> const Rendered_output*
{
    return m_rendered_outputs.find(frame_index);
}

void Ray_trace_renderer::publish_rendered_output()
{
    Rendered_output& output = m_rendered_outputs.write(m_graphics_device.get_frame_index());
    output.width          = m_output_texture ? m_output_texture->get_width () : 0;
    output.height         = m_output_texture ? m_output_texture->get_height() : 0;
    output.instance_count = m_scene_tlas ? m_scene_tlas->get_instance_count() : 0;
}

auto Ray_trace_renderer::request_output_readback() -> bool
{
    const std::lock_guard<std::mutex> lock{m_readback_mutex};
    if ((m_readback_state == Readback_state::requested) || (m_readback_state == Readback_state::in_flight)) {
        return false;
    }
    m_readback_pixels.clear();
    m_readback_state = Readback_state::requested;
    return true;
}

void Ray_trace_renderer::cancel_output_readback()
{
    const std::lock_guard<std::mutex> lock{m_readback_mutex};
    if (m_readback_state == Readback_state::requested) {
        m_readback_state = Readback_state::idle;
    }
}

void Ray_trace_renderer::record_output_readback(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    erhe::utility::Thread_roles::assert_render_thread();
    const std::lock_guard<std::mutex> lock{m_readback_mutex};
    if (m_readback_state != Readback_state::requested) {
        return;
    }
    if (!is_supported() || !m_output_texture) {
        m_readback_state = Readback_state::failed;
        return;
    }
    const int         width         = m_output_texture->get_width();
    const int         height        = m_output_texture->get_height();
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * 4u;
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

    // Blit the output texture into a host-visible buffer in this frame's
    // command buffer, after every pass of the frame that writes or samples
    // it. The texture goes back to shader_read_only_optimal so the ImGui
    // display keeps working.
    m_readback_buffer = std::make_unique<Buffer>(
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = byte_count,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::transfer_dst | Buffer_usage::storage,
            .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
            .debug_label                            = erhe::utility::Debug_label{"ray trace readback"}
        }
    );
    command_buffer.transition_texture_layout(*m_output_texture, Image_layout::transfer_src_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            erhe::graphics::Texture_location{.texture = m_output_texture.get(), .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}},
            glm::ivec3{width, height, 1},
            erhe::graphics::Buffer_texel_location{.buffer = m_readback_buffer.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(bytes_per_row), .bytes_per_image = static_cast<std::uintptr_t>(byte_count)}
        );
    }
    command_buffer.transition_texture_layout(*m_output_texture, Image_layout::shader_read_only_optimal);
    // Transfer writes -> host reads once the frame's fence has signalled.
    command_buffer.memory_barrier(Memory_barrier_mask::client_mapped_buffer_barrier_bit);

    m_readback_width  = width;
    m_readback_height = height;
    m_readback_frame  = m_graphics_device.get_frame_index();
    m_readback_state  = Readback_state::in_flight;
}

auto Ray_trace_renderer::poll_output_readback() -> Readback_state
{
    const std::lock_guard<std::mutex> lock{m_readback_mutex};
    if ((m_readback_state != Readback_state::in_flight) || !m_graphics_device.is_frame_completed(m_readback_frame)) {
        return m_readback_state;
    }
    const std::size_t          byte_count = static_cast<std::size_t>(m_readback_width) * static_cast<std::size_t>(m_readback_height) * 4u;
    const std::span<std::byte> mapped     = m_readback_buffer->map_bytes(0, byte_count);
    m_readback_buffer->invalidate(0, byte_count);
    if (mapped.size() < byte_count) {
        m_readback_buffer->unmap();
        m_readback_buffer.reset();
        m_readback_state = Readback_state::failed;
        return m_readback_state;
    }
    m_readback_pixels.resize(byte_count);
    std::memcpy(m_readback_pixels.data(), mapped.data(), byte_count);
    m_readback_buffer->unmap();
    m_readback_buffer.reset();
    m_readback_state = Readback_state::complete;
    return m_readback_state;
}

void Ray_trace_renderer::take_output_readback(std::vector<uint8_t>& out_pixels, int& out_width, int& out_height)
{
    const std::lock_guard<std::mutex> lock{m_readback_mutex};
    ERHE_VERIFY(m_readback_state == Readback_state::complete);
    out_pixels.swap(m_readback_pixels);
    m_readback_pixels.clear();
    out_width        = m_readback_width;
    out_height       = m_readback_height;
    m_readback_state = Readback_state::idle;
}

void Ray_trace_renderer::render(
    erhe::graphics::Command_buffer&                command_buffer,
    Scene_root&                                    scene_root,
    const erhe::scene::Camera_snapshot&            camera,
    const erhe::math::Viewport&                    viewport,
    const erhe::scene_renderer::Light_projections* light_projections
)
{
    using namespace erhe::graphics;

    if (!is_supported() || !m_render_enabled) {
        return;
    }
    const Ray_trace_scene* ray_trace_scene = scene_root.get_ray_trace_scene();
    if (ray_trace_scene == nullptr) {
        return; // a root without device dependencies traces nothing
    }
    if ((viewport.width < 1) || (viewport.height < 1)) {
        return;
    }
    // The prepare phase's view of the scene for this frame.
    const Ray_trace_slot& slot = ray_trace_scene->get_slot(m_graphics_device.get_frame_index());

    // Output texture at the viewport's resolution divided by the configured
    // downscale factor (1 = one ray per viewport pixel, 2 = each traced
    // pixel covers 2x2 viewport pixels), like the raster render targets;
    // recreated on resize / scale change (the Vulkan texture impl defers
    // the old image's destruction to frame completion, so in-flight frames
    // and the ImGui display holding the previous shared_ptr stay valid).
    const float downscale     = std::clamp(get_config().downscale, 1.0f, 8.0f);
    const int   output_width  = std::max(1, static_cast<int>(static_cast<float>(viewport.width ) / downscale));
    const int   output_height = std::max(1, static_cast<int>(static_cast<float>(viewport.height) / downscale));
    if (!m_output_texture ||
        (m_output_texture->get_width()  != output_width) ||
        (m_output_texture->get_height() != output_height)) {
        m_output_texture = std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                // transfer_src backs record_output_readback() (MCP set_ray_trace save_path).
                .usage_mask  = Image_usage_flag_bit_mask::storage | Image_usage_flag_bit_mask::sampled | Image_usage_flag_bit_mask::transfer_src,
                .type        = Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
                .width       = output_width,
                .height      = output_height,
                .level_count = 1,
                .debug_label = erhe::utility::Debug_label{"Ray trace output"}
            }
        );
    }

    // Materials: the scene root's FORWARD set, already updated for this frame
    // (doc/erhe/draw_list_material_set.md D5, D6). This dispatch binds it and
    // the TLAS instance records name slots in it.
    erhe::scene_renderer::Material_set& material_set = scene_root.get_material_set();
    if (slot.forward_material_live_count == 0) {
        // No materials means no shadeable content; keep the last output.
        publish_rendered_output();
        return;
    }

    // Acceleration structures + per-instance records for this frame (the
    // scene's shared bottom level structures, this renderer's per-frame-in-
    // flight top level slot and instance record ring buffer).
    Scene_tlas::Frame tlas_frame = m_scene_tlas->update(command_buffer, slot);
    ERHE_VERIFY(tlas_frame.is_valid());

    // Lights + ambient. Light_buffer::update() writes per-light data only
    // for lights present in light_projections (their world/texture
    // transforms come from there), so with no projections available pass no
    // lights - ambient still applies.
    const glm::vec3 ambient_light = scene_root.get_render_settings(m_graphics_device.get_frame_index()).ambient_light;
    erhe::scene_renderer::Light_buffer_ranges light_range = m_light_buffer->update(light_projections, ambient_light);

    // Runtime knobs from Ray_trace_config, clamped to sane / compile-time
    // bounds (max_bounces must not exceed the shader's stack bound).
    Ring_buffer_range control_range = m_control_buffer->acquire(Ring_buffer_usage::CPU_write, m_control_block.get_size_bytes());
    {
        std::span<std::byte> gpu_data    = control_range.get_span();
        const uint32_t       max_rays    = static_cast<uint32_t>(std::clamp(get_config().max_rays,    1, 1024));
        const uint32_t       max_bounces = static_cast<uint32_t>(std::clamp(get_config().max_bounces, 0, c_max_bounces));
        std::memset(gpu_data.data(), 0, m_control_block.get_size_bytes());
        write(gpu_data, m_control_max_rays_offset,    as_span(max_rays));
        write(gpu_data, m_control_max_bounces_offset, as_span(max_bounces));
        control_range.bytes_written(m_control_block.get_size_bytes());
        control_range.close();
    }

    // Camera UBO for the ray generation (world_from_clip + world_from_node).
    // The RASTER viewport defines the projection so the traced framing (fov
    // / aspect) matches the viewport exactly - the fixed-size output texture
    // just resamples it. Exposure comes from the camera like the raster
    // path (composition_pass uses camera.get_exposure() the same way).
    const Device_info& info = m_graphics_device.get_info();
    Ring_buffer_range camera_range = m_camera_buffer->update(
        camera,
        viewport,
        camera.get_exposure(),
        erhe::scene_renderer::Grid_parameters{},
        erhe::scene_renderer::Sky_parameters{},
        m_graphics_device.get_frame_index(),
        m_graphics_device.get_reverse_depth(),
        info.coordinate_conventions.native_depth_range,
        info.coordinate_conventions
    );

    command_buffer.transition_texture_layout(*m_output_texture, Image_layout::general);
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_bind_group_layout.get());
        encoder.set_compute_pipeline(*m_pipeline);
        m_camera_buffer->bind(encoder, camera_range);
        m_light_buffer->bind_light_buffer(encoder, light_range);
        m_control_buffer->bind(encoder, control_range);
        m_scene_tlas->bind_instance_records(encoder, tlas_frame);
        encoder.set_acceleration_structure(m_tlas_binding_point, *tlas_frame.acceleration_structure);
        encoder.set_storage_image(m_output_binding_point, *m_output_texture);
        material_set.bind(encoder);
        encoder.dispatch_compute(
            (static_cast<std::uintptr_t>(output_width)  + 7) / 8,
            (static_cast<std::uintptr_t>(output_height) + 7) / 8,
            1
        );
    }
    camera_range.release();
    light_range.release();
    control_range.release();
    tlas_frame.instance_records.release();
    material_set.unbind(command_buffer);

    // The output becomes a sampled texture for the ImGui display.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_output_texture, Image_layout::shader_read_only_optimal);
    publish_rendered_output();
}

} // namespace editor
