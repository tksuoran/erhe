#include "renderers/ray_trace_renderer.hpp"

#include "app_context.hpp"
#include "config/generated/ray_trace_config.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
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
    erhe::scene_renderer::Mesh_memory&       mesh_memory,
    const Ray_trace_config&                  config
)
    : m_graphics_device{graphics_device}
    , m_context        {context}
    , m_config         {config}
    , m_control_block{
        graphics_device,
        "ray_trace_control",
        static_cast<int>(c_control_binding_point),
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_fallback_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter        = erhe::graphics::Filter::nearest,
            .mag_filter        = erhe::graphics::Filter::nearest,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode      = { erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge },
            .compare_enable    = false,
            .compare_operation = erhe::graphics::Compare_operation::always,
            .debug_label       = "Ray_trace_renderer::m_fallback_sampler"
        }
    }
{
    using namespace erhe::graphics;
    using erhe::utility::Debug_label;

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
        mesh_memory,
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
    m_material_buffer = std::make_unique<erhe::scene_renderer::Material_buffer>(
        graphics_device,
        program_interface.material_interface
    );
    m_light_buffer = std::make_unique<erhe::scene_renderer::Light_buffer>(
        graphics_device,
        init_command_buffer,
        program_interface.light_interface
    );
    m_dummy_texture = graphics_device.create_dummy_texture(init_command_buffer, erhe::dataformat::Format::format_8_vec4_srgb);
    m_texture_heap = std::make_unique<Texture_heap>(
        graphics_device,
        *m_dummy_texture.get(),
        m_fallback_sampler,
        m_bind_group_layout.get()
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

auto Ray_trace_renderer::is_supported() const -> bool
{
    return m_pipeline != nullptr;
}

auto Ray_trace_renderer::is_enabled() const -> bool
{
    return m_enabled;
}

void Ray_trace_renderer::set_enabled(const bool enabled)
{
    m_enabled = enabled;
}

auto Ray_trace_renderer::get_output_texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_output_texture;
}

auto Ray_trace_renderer::get_instance_count() const -> std::size_t
{
    return m_scene_tlas ? m_scene_tlas->get_instance_count() : 0;
}

auto Ray_trace_renderer::get_blas_count() const -> std::size_t
{
    return m_scene_tlas ? m_scene_tlas->get_blas_count() : 0;
}

auto Ray_trace_renderer::read_output_rgba8(std::vector<uint8_t>& out_pixels) -> bool
{
    using namespace erhe::graphics;

    if (!is_supported() || !m_output_texture) {
        return false;
    }
    const int         width         = m_output_texture->get_width();
    const int         height        = m_output_texture->get_height();
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * 4u;
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

    // Self-contained mini-submit on a dedicated thread slot, mirroring
    // Texture_renderer::render_and_read_rgba8: blit the output texture into a
    // host-visible buffer, wait, map. The texture is transitioned back to
    // shader_read_only_optimal so the ImGui display keeps working.
    Buffer readback{
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = byte_count,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::transfer_dst | Buffer_usage::storage,
            .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
            .debug_label                            = erhe::utility::Debug_label{"ray trace readback"}
        }
    };

    constexpr unsigned int readback_thread_slot = 7;
    Command_buffer& command_buffer = m_graphics_device.get_command_buffer(readback_thread_slot);
    command_buffer.begin();
    command_buffer.transition_texture_layout(*m_output_texture, Image_layout::transfer_src_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            m_output_texture.get(),
            0,                             // source_slice
            0,                             // source_level
            glm::ivec3{0, 0, 0},           // source_origin
            glm::ivec3{width, height, 1},  // source_size
            &readback,                     // destination_buffer
            0,                             // destination_offset
            static_cast<std::uintptr_t>(bytes_per_row),
            static_cast<std::uintptr_t>(byte_count)
        );
    }
    command_buffer.transition_texture_layout(*m_output_texture, Image_layout::shader_read_only_optimal);
    command_buffer.end();

    erhe::graphics::Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    const std::span<std::byte> mapped = readback.map_bytes(0, byte_count);
    if (mapped.size() < byte_count) {
        return false;
    }
    out_pixels.resize(byte_count);
    std::memcpy(out_pixels.data(), mapped.data(), byte_count);
    readback.unmap();
    return true;
}

void Ray_trace_renderer::render(
    erhe::graphics::Command_buffer&                command_buffer,
    Scene_root&                                    scene_root,
    const erhe::scene::Camera&                     camera,
    const erhe::math::Viewport&                    viewport,
    const erhe::scene_renderer::Light_projections* light_projections
)
{
    using namespace erhe::graphics;

    if (!is_supported() || !m_enabled) {
        return;
    }
    const erhe::scene::Projection* camera_projection = camera.projection();
    const erhe::scene::Node*       camera_node       = camera.get_node();
    if ((camera_projection == nullptr) || (camera_node == nullptr)) {
        return;
    }
    erhe::scene::Mesh_layer* content_layer = scene_root.layers().content();
    if (content_layer == nullptr) {
        return;
    }
    if ((viewport.width < 1) || (viewport.height < 1)) {
        return;
    }

    // Output texture at the viewport's resolution divided by the configured
    // downscale factor (1 = one ray per viewport pixel, 2 = each traced
    // pixel covers 2x2 viewport pixels), like the raster render targets;
    // recreated on resize / scale change (the Vulkan texture impl defers
    // the old image's destruction to frame completion, so in-flight frames
    // and the ImGui display holding the previous shared_ptr stay valid).
    const float downscale     = std::clamp(m_config.downscale, 1.0f, 8.0f);
    const int   output_width  = std::max(1, static_cast<int>(static_cast<float>(viewport.width ) / downscale));
    const int   output_height = std::max(1, static_cast<int>(static_cast<float>(viewport.height) / downscale));
    if (!m_output_texture ||
        (m_output_texture->get_width()  != output_width) ||
        (m_output_texture->get_height() != output_height)) {
        m_output_texture = std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                // transfer_src backs read_output_rgba8() (MCP set_ray_trace save_path).
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

    // Materials: upload the scene's whole content library, like the raster
    // path does; Material_buffer::update() assigns each material's
    // material_buffer_index, which the instance records below capture.
    const std::shared_ptr<Content_library>& content_library = scene_root.get_content_library();
    if (!content_library || !content_library->materials) {
        return;
    }
    const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = content_library->materials->get_all<erhe::primitive::Material>();
    if (materials.empty()) {
        // No materials means no shadeable content (and Material_buffer
        // returns an unbindable empty range); keep the last output.
        return;
    }

    m_texture_heap->reset_heap(command_buffer);
    Ring_buffer_range material_range = m_material_buffer->update(*m_texture_heap.get(), materials);

    // Acceleration structures + per-instance records for this frame (shared
    // Scene_tlas helper: bottom level cache, per-frame-in-flight top level
    // slot, instance record ring buffer).
    Scene_tlas::Frame tlas_frame = m_scene_tlas->update(command_buffer, *content_layer);
    ERHE_VERIFY(tlas_frame.is_valid());

    // Lights + ambient. Light_buffer::update() writes per-light data only
    // for lights present in light_projections (their world/texture
    // transforms come from there), so with no projections available pass no
    // lights - ambient still applies.
    const glm::vec3 ambient_light = glm::vec3{scene_root.get_scene().ambient_light};
    Ring_buffer_range light_range = m_light_buffer->update(light_projections, ambient_light);

    // Runtime knobs from Ray_trace_config, clamped to sane / compile-time
    // bounds (max_bounces must not exceed the shader's stack bound).
    Ring_buffer_range control_range = m_control_buffer->acquire(Ring_buffer_usage::CPU_write, m_control_block.get_size_bytes());
    {
        std::span<std::byte> gpu_data    = control_range.get_span();
        const uint32_t       max_rays    = static_cast<uint32_t>(std::clamp(m_config.max_rays,    1, 1024));
        const uint32_t       max_bounces = static_cast<uint32_t>(std::clamp(m_config.max_bounces, 0, c_max_bounces));
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
        *camera_projection,
        *camera_node,
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
        m_material_buffer->bind(encoder, material_range);
        m_light_buffer->bind_light_buffer(encoder, light_range);
        m_control_buffer->bind(encoder, control_range);
        m_scene_tlas->bind_instance_records(encoder, tlas_frame);
        encoder.set_acceleration_structure(m_tlas_binding_point, *tlas_frame.acceleration_structure);
        encoder.set_storage_image(m_output_binding_point, *m_output_texture);
        m_texture_heap->bind(encoder);
        encoder.dispatch_compute(
            (static_cast<std::uintptr_t>(output_width)  + 7) / 8,
            (static_cast<std::uintptr_t>(output_height) + 7) / 8,
            1
        );
    }
    camera_range.release();
    material_range.release();
    light_range.release();
    control_range.release();
    tlas_frame.instance_records.release();
    m_texture_heap->unbind(command_buffer);

    // The output becomes a sampled texture for the ImGui display.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_output_texture, Image_layout::shader_read_only_optimal);
}

} // namespace editor
