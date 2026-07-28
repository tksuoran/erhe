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

// Instance mask bits, mirrored in ray_trace.comp: bit 0 = every instance,
// bit 1 = non-transmissive instances only (shadow rays trace with mask 0x02
// so glass does not cast shadows).
constexpr uint32_t c_instance_mask_all    = 0x01u;
constexpr uint32_t c_instance_mask_opaque = 0x02u;

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
    , m_mesh_memory    {mesh_memory}
    , m_config         {config}
    , m_instance_struct{graphics_device, "Instance_record"}
    , m_instance_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "instance",
            .binding_point = static_cast<int>(c_instance_record_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
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
    // geometric normal, so position fetch is required on top of ray query.
    if (!graphics_device.get_info().use_ray_query || !graphics_device.get_info().use_ray_tracing_position_fetch) {
        log_startup->info("Ray_trace_renderer: ray query not available, GPU ray tracing disabled");
        return;
    }

    const std::filesystem::path editor_shaders = std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"};

    // Per-instance record layout (std430). Mirrors Instance_record_data;
    // verified below so the CPU-side memcpy upload matches the generated
    // GPU layout.
    const std::size_t off_index_address       = m_instance_struct.add_uvec2("index_address"      )->get_offset_in_parent();
    const std::size_t off_vertex_address      = m_instance_struct.add_uvec2("vertex_address"     )->get_offset_in_parent();
    const std::size_t off_vertex_stride_uints = m_instance_struct.add_uint ("vertex_stride_uints")->get_offset_in_parent();
    const std::size_t off_material_index      = m_instance_struct.add_uint ("material_index"     )->get_offset_in_parent();
    const std::size_t off_flags               = m_instance_struct.add_uint ("flags"              )->get_offset_in_parent();
    const std::size_t off_reserved0           = m_instance_struct.add_uint ("reserved0"          )->get_offset_in_parent();
    ERHE_VERIFY(off_index_address       == offsetof(Instance_record_data, index_address));
    ERHE_VERIFY(off_vertex_address      == offsetof(Instance_record_data, vertex_address));
    ERHE_VERIFY(off_vertex_stride_uints == offsetof(Instance_record_data, vertex_stride_uints));
    ERHE_VERIFY(off_material_index      == offsetof(Instance_record_data, material_index));
    ERHE_VERIFY(off_flags               == offsetof(Instance_record_data, flags));
    ERHE_VERIFY(off_reserved0           == offsetof(Instance_record_data, reserved0));
    ERHE_VERIFY(m_instance_struct.get_size_bytes() == sizeof(Instance_record_data));
    m_instance_block.add_struct("instances", &m_instance_struct, erhe::graphics::Shader_resource::unsized_array);

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
                { "ERHE_TLAS_BINDING",        fmt::format("{}", m_tlas_binding_point) },
                { "ERHE_RT_NORMAL_OFFSET",    fmt::format("{}", normal   .attribute->offset / 4) },
                { "ERHE_RT_TANGENT_OFFSET",   fmt::format("{}", tangent  .attribute->offset / 4) },
                { "ERHE_RT_TEXCOORD0_OFFSET", fmt::format("{}", texcoord0.attribute->offset / 4) },
                { "ERHE_RT_COLOR0_OFFSET",    fmt::format("{}", color0   .attribute->offset / 4) },
                { "ERHE_RT_MAX_BOUNCES",      fmt::format("{}", c_max_bounces) }
            },
            .extensions          = {
                { Shader_type::compute_shader, "GL_EXT_ray_query" },
                { Shader_type::compute_shader, "GL_EXT_ray_tracing_position_fetch" },
                { Shader_type::compute_shader, "GL_EXT_buffer_reference" },
                { Shader_type::compute_shader, "GL_EXT_buffer_reference_uvec2" }
            },
            .struct_types        = {
                &program_interface.camera_interface.camera_struct,
                &program_interface.material_interface.material_struct,
                &program_interface.light_interface.light_struct,
                &m_instance_struct
            },
            .interface_blocks    = {
                &program_interface.camera_interface.camera_block,
                &program_interface.material_interface.material_block,
                &program_interface.light_interface.light_block,
                &m_control_block,
                &m_instance_block
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
    m_instance_record_buffer = std::make_unique<Ring_buffer_client>(
        graphics_device,
        Buffer_target::storage,
        "Ray_trace_renderer::instance_records",
        c_instance_record_binding_point
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
    return m_instances.size();
}

auto Ray_trace_renderer::get_or_create_blas(
    erhe::graphics::Command_buffer&                    command_buffer,
    const std::shared_ptr<erhe::primitive::Primitive>& primitive,
    const erhe::primitive::Buffer_mesh&                buffer_mesh
) -> erhe::graphics::Acceleration_structure*
{
    using namespace erhe::graphics;

    const auto existing = m_blas_cache.find(&buffer_mesh);
    if (existing != m_blas_cache.end()) {
        return existing->second.acceleration_structure.get();
    }

    // The acceleration structure reads triangles straight from the mesh
    // memory pools: stream 0 starts with position (3 x float32 at offset 0)
    // for both the skinned and non-skinned vertex formats, and indices are a
    // uint32 triangle list. Indices are relative to the range start (draws
    // use base_vertex), so baking the range byte offsets into the build
    // addresses matches.
    if (buffer_mesh.vertex_buffer_ranges.empty()) {
        return nullptr;
    }
    const erhe::primitive::Buffer_range& vertex_range = buffer_mesh.vertex_buffer_ranges[0];
    const erhe::primitive::Buffer_range& index_range  = buffer_mesh.index_buffer_range;
    const erhe::primitive::Index_range&  triangles    = buffer_mesh.triangle_fill_indices;
    if ((triangles.index_count == 0) || (triangles.primitive_type != erhe::primitive::Primitive_type::triangles)) {
        return nullptr;
    }
    if ((vertex_range.count == 0) || (index_range.element_size != sizeof(uint32_t))) {
        return nullptr;
    }
    erhe::graphics::Buffer* vertex_buffer = m_mesh_memory.get_vertex_buffer(vertex_range);
    erhe::graphics::Buffer* index_buffer  = m_mesh_memory.get_index_buffer(index_range);
    if ((vertex_buffer == nullptr) || (index_buffer == nullptr)) {
        return nullptr;
    }

    Blas_entry& entry = m_blas_cache[&buffer_mesh];
    entry.primitive = primitive;
    entry.acceleration_structure = std::make_unique<Acceleration_structure>(
        m_graphics_device,
        Acceleration_structure_create_info{
            .type                = Acceleration_structure_type::bottom_level,
            .triangle_geometries = {
                Acceleration_structure_triangles{
                    .vertex_buffer      = vertex_buffer,
                    .vertex_byte_offset = vertex_range.byte_offset,
                    .vertex_byte_stride = vertex_range.element_size,
                    .vertex_count       = vertex_range.count,
                    .index_buffer       = index_buffer,
                    .index_byte_offset  = index_range.byte_offset + (triangles.first_index * index_range.element_size),
                    .index_count        = triangles.index_count,
                    .opaque             = true
                }
            },
            .debug_label = erhe::utility::Debug_label{"Ray trace BLAS"}
        }
    );
    entry.acceleration_structure->build(command_buffer);
    entry.built = true;
    return entry.acceleration_structure.get();
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

    // Gather visible, non-skinned content mesh instances; build missing
    // bottom level structures into this command buffer (their builds are
    // ordered before the top level build below, and Acceleration_structure::
    // build() ends with the build->build barrier). Each instance also gets a
    // record (indexed by instance_custom_index = ordinal) carrying its
    // material index and the device addresses for attribute fetch.
    m_instances.clear();
    m_instance_records.clear();
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : content_layer->meshes) {
        if (!mesh || !mesh->is_visible() || mesh->skin) {
            continue;
        }
        const erhe::scene::Node* node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (!mesh_primitive.primitive) {
                continue;
            }
            const erhe::primitive::Buffer_mesh* buffer_mesh = mesh_primitive.primitive->get_renderable_mesh();
            if (buffer_mesh == nullptr) {
                continue;
            }
            Acceleration_structure* blas = get_or_create_blas(command_buffer, mesh_primitive.primitive, *buffer_mesh);
            if (blas == nullptr) {
                continue;
            }
            // Attribute fetch needs stream 1 (normal / texcoords / color).
            if (buffer_mesh->vertex_buffer_ranges.size() < 2) {
                continue;
            }
            const erhe::primitive::Buffer_range& attribute_range = buffer_mesh->vertex_buffer_ranges[1];
            const erhe::primitive::Buffer_range& index_range     = buffer_mesh->index_buffer_range;
            erhe::graphics::Buffer* attribute_buffer = m_mesh_memory.get_vertex_buffer(attribute_range);
            erhe::graphics::Buffer* index_buffer     = m_mesh_memory.get_index_buffer(index_range);
            if ((attribute_buffer == nullptr) || (index_buffer == nullptr)) {
                continue;
            }
            const uint64_t attribute_base_address = attribute_buffer->get_device_address();
            const uint64_t index_base_address     = index_buffer->get_device_address();
            if ((attribute_base_address == 0) || (index_base_address == 0) || ((attribute_range.element_size % 4) != 0)) {
                continue;
            }

            const erhe::primitive::Material* material       = mesh_primitive.material.get();
            const uint32_t                   material_index = (material != nullptr) ? material->material_buffer_index : 0u;
            const bool                       transmissive   = (material != nullptr) && (material->data.transmission > 0.0f);

            m_instance_records.push_back(
                Instance_record_data{
                    .index_address       = index_base_address + index_range.byte_offset + (buffer_mesh->triangle_fill_indices.first_index * index_range.element_size),
                    .vertex_address      = attribute_base_address + attribute_range.byte_offset,
                    .vertex_stride_uints = static_cast<uint32_t>(attribute_range.element_size / 4),
                    .material_index      = material_index,
                    .flags               = transmissive ? 1u : 0u,
                    .reserved0           = 0u
                }
            );
            m_instances.push_back(
                Acceleration_structure_instance{
                    .transform             = world_from_node,
                    .instance_custom_index = static_cast<uint32_t>(m_instances.size()),
                    .mask                  = transmissive ? c_instance_mask_all : (c_instance_mask_all | c_instance_mask_opaque),
                    .bottom_level          = blas
                }
            );
        }
    }

    // Top level structure for this frame-in-flight slot; capacity grows to a
    // high-water mark (steady-state frames re-use the existing structure).
    const std::size_t slot_index = static_cast<std::size_t>(m_graphics_device.get_frame_index() % s_tlas_slot_count);
    Tlas_slot& slot = m_tlas_slots[slot_index];
    const uint32_t required_capacity = static_cast<uint32_t>(m_instances.size());
    if (!slot.acceleration_structure || (slot.capacity < required_capacity)) {
        const uint32_t new_capacity = std::max(64u, std::bit_ceil(required_capacity));
        slot.acceleration_structure = std::make_unique<Acceleration_structure>(
            m_graphics_device,
            Acceleration_structure_create_info{
                .type               = Acceleration_structure_type::top_level,
                .max_instance_count = new_capacity,
                .debug_label        = erhe::utility::Debug_label{"Ray trace TLAS"}
            }
        );
        slot.capacity = new_capacity;
    }
    slot.acceleration_structure->build(command_buffer, m_instances);

    // Per-instance records into the ring buffer. Always at least one
    // (zeroed) record so the binding stays valid when the scene is empty.
    const std::size_t record_count      = std::max(m_instance_records.size(), std::size_t{1});
    const std::size_t record_byte_count = record_count * sizeof(Instance_record_data);
    Ring_buffer_range instance_range = m_instance_record_buffer->acquire(Ring_buffer_usage::CPU_write, record_byte_count);
    {
        std::span<std::byte> gpu_data = instance_range.get_span();
        if (m_instance_records.empty()) {
            std::memset(gpu_data.data(), 0, record_byte_count);
        } else {
            std::memcpy(gpu_data.data(), m_instance_records.data(), record_byte_count);
        }
        instance_range.bytes_written(record_byte_count);
        instance_range.close();
    }

    // Lights + ambient. Light_buffer::update() writes per-light data only
    // for lights present in light_projections (their world/texture
    // transforms come from there), so with no projections available pass no
    // lights - ambient still applies.
    erhe::scene::Light_layer* light_layer = scene_root.layers().light();
    const std::span<const std::shared_ptr<erhe::scene::Light>> lights =
        ((light_projections != nullptr) && (light_layer != nullptr))
            ? std::span<const std::shared_ptr<erhe::scene::Light>>{light_layer->lights}
            : std::span<const std::shared_ptr<erhe::scene::Light>>{};
    const glm::vec3 ambient_light = glm::vec3{scene_root.get_scene().ambient_light};
    Ring_buffer_range light_range = m_light_buffer->update(lights, light_projections, ambient_light);

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
        m_instance_record_buffer->bind(encoder, instance_range);
        encoder.set_acceleration_structure(m_tlas_binding_point, *slot.acceleration_structure);
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
    instance_range.release();
    m_texture_heap->unbind(command_buffer);

    // The output becomes a sampled texture for the ImGui display.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_output_texture, Image_layout::shader_read_only_optimal);
}

} // namespace editor
