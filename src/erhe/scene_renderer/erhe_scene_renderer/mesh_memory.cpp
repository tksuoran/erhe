#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/buffer_pool.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/state/vertex_input_state.hpp"

#include "erhe_scene/mesh.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_primitive/primitive.hpp"

#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

using Format                 = erhe::dataformat::Format;
using Vertex_attribute_usage = erhe::dataformat::Vertex_attribute_usage;

Mesh_memory::Mesh_memory(
    const Mesh_memory_config& mesh_memory_config,
    erhe::graphics::Device&   graphics_device
)
    : vertex_format_empty{}
    , vertex_format_skinned{
        erhe::dataformat::Vertex_format{
            {
                0,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::position,      0},
                    { Format::format_8_vec4_uint,   Vertex_attribute_usage::joint_indices, 0},
                    { Format::format_8_vec4_unorm,  Vertex_attribute_usage::joint_weights, 0}
                }
            },
            {
                1,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::normal,    erhe::dataformat::normal_attribute},
                    { Format::format_32_vec4_float, Vertex_attribute_usage::tangent,   0},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0},
                    { Format::format_32_vec4_float, Vertex_attribute_usage::color,     0},
                }
            },
            {
                2,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::normal, erhe::dataformat::normal_attribute_smooth}, // wireframe bias requires smooth normal attribute
                    { Format::format_8_vec2_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control},
                    { Format::format_16_vec2_uint,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_valency_edge_count},
                    { Format::format_8_vec4_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_id}
                }
            }
        }
    }
    , vertex_format_not_skinned{
        erhe::dataformat::Vertex_format{
            {
                0,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::position, 0}
                }
            },
            {
                1,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::normal,    erhe::dataformat::normal_attribute},
                    { Format::format_32_vec4_float, Vertex_attribute_usage::tangent,   0},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0},
                    { Format::format_32_vec4_float, Vertex_attribute_usage::color,     0},
                }
            },
            {
                2,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::normal, erhe::dataformat::normal_attribute_smooth},
                    { Format::format_8_vec2_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control},
                    { Format::format_16_vec2_uint,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_valency_edge_count},
                    { Format::format_8_vec4_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_id}
                }
            }
        }
    }
    , vertex_format_edge_line{
        erhe::dataformat::Vertex_format{
            {
                0,
                {
                    { Format::format_32_vec4_float, Vertex_attribute_usage::position, 0},
                    { Format::format_32_vec4_float, Vertex_attribute_usage::normal,   erhe::dataformat::normal_attribute_smooth}
                }
            }
        }
    }
    , vertex_format_edge_line_joints{
        erhe::dataformat::Vertex_format{
            {
                0,
                {
                    { Format::format_32_vec4_uint,  Vertex_attribute_usage::joint_indices, 0},
                    { Format::format_32_vec4_float, Vertex_attribute_usage::joint_weights, 0}
                }
            }
        }
    }
    , m_mesh_memory_config   {mesh_memory_config}
    , m_graphics_device      {graphics_device}
    , m_buffer_transfer_queue{graphics_device}
{
    get_vertex_input_from_vertex_format(vertex_format_empty);
    get_vertex_input_from_vertex_format(vertex_format_skinned);
    get_vertex_input_from_vertex_format(vertex_format_not_skinned);
    get_vertex_input_from_vertex_format(vertex_format_edge_line);
    get_vertex_input_from_vertex_format(vertex_format_edge_line_joints);
}

Mesh_memory::~Mesh_memory() noexcept = default;

constexpr std::size_t kilo = 1024;
constexpr std::size_t mega = 1024 * kilo;

// Pool selection rule: one Buffer_pool per Vertex_stream INSTANCE address
// (NOT per Vertex_stream byte layout). The Vertex_format objects whose
// streams we pass here -- vertex_format_skinned, vertex_format_not_skinned,
// vertex_format_edge_line, vertex_format_edge_line_joints, etc. -- are
// stable members of this Mesh_memory and own their Vertex_stream
// instances for the lifetime of the renderer, so pointer identity is a
// safe discriminator.
//
// Two distinct Vertex_formats whose stream layouts happen to be byte-
// for-byte identical (vertex_format_skinned.streams[1] and
// vertex_format_not_skinned.streams[1] both encode normal/tangent/
// texcoord/color the same way) still allocate from DIFFERENT pools.
// This duplicates a small amount of buffer memory but is required for
// correctness:
//
//   The forward renderer's multi-draw indexed indirect commands carry a
//   single `vertexOffset` scalar per draw, applied uniformly to every
//   binding. That scalar is computed from stream 0 only
//   (Buffer_mesh::base_vertex()). For the read to land on the right
//   bytes in every binding, per-mesh `byte_offset_K / stride_K` must be
//   identical for all streams K of that mesh.
//
//   If two formats with different stream-0 strides shared a pool for
//   stream 1, the shared pool would advance for BOTH formats' meshes
//   while each format's private stream-0 pool would only advance for
//   that format's meshes. The invariant would then fail for the
//   second-allocated format's meshes -- stream 0 reads at the correct
//   byte but stream 1 reads from some other mesh's data. The visible
//   symptom is correct vertex positions with garbage normals / tangents /
//   tex_coords / colors. See buffer_pool.hpp for the long-form
//   explanation.
//
// is_compatible() does the pointer check; the lookup below relies on it.
auto Mesh_memory::allocate_vertex_buffer_range(
    const erhe::dataformat::Vertex_stream& vertex_stream,
    const std::size_t                      vertex_count
) -> erhe::primitive::Buffer_sink_allocation
{
    log_mesh_memory->trace(
        "Mesh_memory::allocate_vertex_buffer_range(vertex_stream = {}, vertex_count = {}) pool_id = {}",
        vertex_stream.to_string(),
        vertex_count,
        m_vertex_pools.size()
    );
    for (Buffer_pool& pool : m_vertex_pools) {
        if (pool.is_compatible(vertex_stream)) {
            return pool.allocate(vertex_count);
        }
    }
    log_mesh_memory->trace(
        "Creating new vertex buffer pool for stream {}",
        vertex_stream.to_string()
    );
    m_vertex_pools.emplace_back(
        m_graphics_device,
        m_vertex_pools.size(),
        vertex_stream,
        Buffer_pool_block_create_info{
            // `storage` is required because Content_wide_line_compute_renderer
            // reads vertex data as an SSBO in its compute pre-pass; without
            // it Vulkan validation fires VUID-VkWriteDescriptorSet-descriptorType-00331
            // ("buffer was created with VK_BUFFER_USAGE_VERTEX_BUFFER_BIT but
            // descriptorType is VK_DESCRIPTOR_TYPE_STORAGE_BUFFER").
            .usage                              = erhe::graphics::Buffer_usage::vertex | erhe::graphics::Buffer_usage::storage,
            .required_memory_property_bit_mask  = erhe::graphics::Memory_property_flag_bit_mask::device_local,
            .preferred_memory_property_bit_mask = erhe::graphics::Memory_property_flag_bit_mask::none,
            .block_size_bytes                   = static_cast<std::size_t>(m_mesh_memory_config.vertex_pool_block_size_mb) * mega,
            .max_blocks                         = static_cast<std::size_t>(m_mesh_memory_config.max_buffers_per_pool),
            .debug_label_prefix                 = fmt::format("Mesh vertex pool {}", m_vertex_pools.size())
        }
    );
    return m_vertex_pools.back().allocate(vertex_count);
}

void Mesh_memory::enqueue_vertex_data(const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data)
{
    log_mesh_memory->trace(
        "Enqueue vertex data for pool_id = {}, buffer_id = {}, byte_offset = {}, byte_count = {}",
        buffer_range.pool_id,
        buffer_range.buffer_id,
        buffer_range.byte_offset,
        data.size()
    );
    const Buffer_pool&      buffer_pool = m_vertex_pools.at(buffer_range.pool_id);
    erhe::graphics::Buffer* buffer      = buffer_pool.get_buffer(buffer_range.buffer_id);
    m_buffer_transfer_queue.enqueue(buffer, buffer_range.byte_offset, std::move(data));
}

void Mesh_memory::vertex_writer_ready(erhe::primitive::Vertex_buffer_writer& writer)
{
    log_mesh_memory->trace(
        "Vertex buffer writer ready for pool_id = {}, buffer_id = {}, byte_offset = {}, byte_count = {}",
        writer.buffer_range.pool_id,
        writer.buffer_range.buffer_id,
        writer.start_offset(),
        writer.vertex_data.size()
    );
    const Buffer_pool&      buffer_pool = m_vertex_pools.at(writer.buffer_range.pool_id);
    erhe::graphics::Buffer* buffer      = buffer_pool.get_buffer(writer.buffer_range.buffer_id);
    m_buffer_transfer_queue.enqueue(buffer, writer.start_offset(), std::move(writer.vertex_data));
}

auto Mesh_memory::allocate_index_buffer_range(
    const erhe::dataformat::Format index_format,
    const std::size_t              index_count
) -> erhe::primitive::Buffer_sink_allocation
{
    log_mesh_memory->trace(
        "Mesh_memory::allocate_index_buffer_range(index_format = {}, index_count = {}) pool_id = {}",
        erhe::dataformat::c_str(index_format),
        index_count,
        m_index_pools.size()
    );
    for (Buffer_pool& pool : m_index_pools) {
        if (pool.is_compatible(index_format)) {
            return pool.allocate(index_count);
        }
    }
    m_index_pools.emplace_back(
        m_graphics_device,
        m_index_pools.size(),
        index_format,
        Buffer_pool_block_create_info{
            .usage                              = erhe::graphics::Buffer_usage::index,
            .required_memory_property_bit_mask  = erhe::graphics::Memory_property_flag_bit_mask::device_local,
            .preferred_memory_property_bit_mask = erhe::graphics::Memory_property_flag_bit_mask::none,
            .block_size_bytes                   = static_cast<std::size_t>(m_mesh_memory_config.index_pool_block_size_mb) * mega,
            .max_blocks                         = static_cast<std::size_t>(m_mesh_memory_config.max_buffers_per_pool),
            .debug_label_prefix                 = fmt::format("Mesh index pool {}", m_index_pools.size())
        }
    );
    return m_index_pools.back().allocate(index_count);
}

void Mesh_memory::enqueue_index_data(const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data)
{
    log_mesh_memory->trace(
        "Enqueue index data for pool_id = {}, buffer_id = {}, byte_offset = {}, byte_count = {}",
        buffer_range.pool_id,
        buffer_range.buffer_id,
        buffer_range.byte_offset,
        data.size()
    );
    const Buffer_pool&      buffer_pool = m_index_pools.at(buffer_range.pool_id);
    erhe::graphics::Buffer* buffer      = buffer_pool.get_buffer(buffer_range.buffer_id);
    m_buffer_transfer_queue.enqueue(buffer, buffer_range.byte_offset, std::move(data));
}

void Mesh_memory::index_writer_ready(erhe::primitive::Index_buffer_writer& writer)
{
    log_mesh_memory->trace(
        "Index buffer writer ready for pool_id = {}, buffer_id = {}, byte_offset = {}, byte_count = {}",
        writer.buffer_range.pool_id,
        writer.buffer_range.buffer_id,
        writer.start_offset(),
        writer.index_data.size()
    );
    const Buffer_pool&      buffer_pool = m_index_pools.at(writer.buffer_range.pool_id);
    erhe::graphics::Buffer* buffer      = buffer_pool.get_buffer(writer.buffer_range.buffer_id);
    m_buffer_transfer_queue.enqueue(buffer, writer.start_offset(), std::move(writer.index_data));
}

auto Mesh_memory::get_empty_vertex_input() -> const Vertex_input_entry&
{
    return m_vertex_input_entries.front();
}

auto Mesh_memory::get_vertex_input_from_vertex_format(const erhe::dataformat::Vertex_format& vertex_format) -> const Vertex_input_entry&
{
    size_t end = m_vertex_input_entries.size();
    for (size_t i = 0; i < end; ++i) {
        const Vertex_input_entry& entry = m_vertex_input_entries[i];
        if (entry.vertex_format == vertex_format) {
            return entry;
        }
    }

    m_vertex_input_entries.emplace_back(
        end,
        std::make_unique<erhe::graphics::Vertex_input_state>(
            m_graphics_device,
            erhe::graphics::Vertex_input_state_data::make(vertex_format)
        ),
        vertex_format
    );

    return m_vertex_input_entries.back();
}

auto Mesh_memory::get_vertex_input(size_t vertex_input_key) const -> const Vertex_input_entry&
{
    return m_vertex_input_entries.at(vertex_input_key);
}

auto Mesh_memory::get_vertex_buffer(const erhe::primitive::Buffer_range& buffer_range) -> erhe::graphics::Buffer*
{
    const Buffer_pool& buffer_pool = m_vertex_pools.at(buffer_range.pool_id);
    return buffer_pool.get_buffer(buffer_range.buffer_id);
}

auto Mesh_memory::get_vertex_buffer(const Pool_buffer_identity& buffer_identity) -> erhe::graphics::Buffer*
{
    const Buffer_pool& buffer_pool = m_vertex_pools.at(buffer_identity.pool_id);
    return buffer_pool.get_buffer(buffer_identity.buffer_id);
}

auto Mesh_memory::get_index_buffer(const erhe::primitive::Buffer_range& buffer_range) -> erhe::graphics::Buffer*
{
    const Buffer_pool& buffer_pool = m_index_pools.at(buffer_range.pool_id);
    return buffer_pool.get_buffer(buffer_range.buffer_id);
}

auto Mesh_memory::get_index_buffer(const Pool_buffer_identity& buffer_identity) -> erhe::graphics::Buffer*
{
    const Buffer_pool& buffer_pool = m_index_pools.at(buffer_identity.pool_id);
    return buffer_pool.get_buffer(buffer_identity.buffer_id);
}

auto Mesh_memory::get_vertex_stream(const Pool_buffer_identity& buffer_identity) -> erhe::dataformat::Vertex_stream
{
    const Buffer_pool& buffer_pool = m_vertex_pools.at(buffer_identity.pool_id);
    return buffer_pool.get_vertex_stream();
}

auto Mesh_memory::get_index_format(const Pool_buffer_identity& buffer_identity) -> erhe::dataformat::Format
{
    const Buffer_pool& buffer_pool = m_index_pools.at(buffer_identity.pool_id);
    return buffer_pool.get_index_format();
}

auto Mesh_memory::make_primitive_buffer_info() -> erhe::primitive::Buffer_info
{
    return erhe::primitive::Buffer_info{
        .index_type              = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format           = vertex_format_not_skinned,
        .vertex_buffer_sink      = *this,
        .index_buffer_sink       = *this,
        .vertex_input_key        = get_vertex_input_from_vertex_format(vertex_format_not_skinned).key,
        .edge_line_vertex_stream = &vertex_format_edge_line       .streams.front(),
        .edge_line_joint_stream  = &vertex_format_edge_line_joints.streams.front()
    };
}

auto Mesh_memory::make_skinned_primitive_buffer_info() -> erhe::primitive::Buffer_info
{
    return erhe::primitive::Buffer_info{
        .index_type              = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format           = vertex_format_skinned,
        .vertex_buffer_sink      = *this,
        .index_buffer_sink       = *this,
        .vertex_input_key        = get_vertex_input_from_vertex_format(vertex_format_skinned).key,
        .edge_line_vertex_stream = &vertex_format_edge_line       .streams.front(),
        .edge_line_joint_stream  = &vertex_format_edge_line_joints.streams.front()
    };
}

void Mesh_memory::flush(erhe::graphics::Command_buffer& command_buffer)
{
    log_mesh_memory->trace("Mesh_memory::flush()");
    m_buffer_transfer_queue.flush(command_buffer);
}

///////////////////////////////////////////////////////////////////////////////

Render_bucket::Render_bucket() = default;

Render_bucket::Render_bucket(
    erhe::scene::Mesh&                  mesh,
    const std::size_t                   mesh_primitive_index,
    const erhe::primitive::Buffer_mesh& buffer_mesh,
    const Shader_key&                   shader_key,
    const uint64_t                      shader_key_hash
)
    : shader_key     {shader_key}
    , shader_key_hash{shader_key_hash}
{
    buffer_set.vertex_input_key = buffer_mesh.vertex_input_key;
    buffer_set.index_buffer     = Pool_buffer_identity{buffer_mesh.index_buffer_range.pool_id, buffer_mesh.index_buffer_range.buffer_id};

    for (const erhe::primitive::Buffer_range& vr : buffer_mesh.vertex_buffer_ranges) {
        buffer_set.vertex_buffers.emplace_back(vr.pool_id, vr.buffer_id);
    }

    const bool done = accept(mesh, mesh_primitive_index, buffer_mesh, shader_key_hash);
    ERHE_VERIFY(done);
}

Render_bucket::~Render_bucket() = default;

auto Render_bucket::accept(
    erhe::scene::Mesh&                  mesh,
    const std::size_t                   mesh_primitive_index,
    const erhe::primitive::Buffer_mesh& buffer_mesh,
    const uint64_t                      primitive_shader_key_hash
) -> bool
{
    if (buffer_mesh.vertex_input_key != buffer_set.vertex_input_key) {
        return false;
    }
    if (Pool_buffer_identity{buffer_mesh.index_buffer_range.pool_id, buffer_mesh.index_buffer_range.buffer_id} != buffer_set.index_buffer) {  
        return false;
    } 
    if (buffer_mesh.vertex_buffer_ranges.size() != buffer_set.vertex_buffers.size()) {
        return false;
    }
    if (primitive_shader_key_hash != shader_key_hash) {
        return false;
    }
    for (size_t i = 0; i < buffer_set.vertex_buffers.size(); ++i) {
        if (Pool_buffer_identity{buffer_mesh.vertex_buffer_ranges[i].pool_id, buffer_mesh.vertex_buffer_ranges[i].buffer_id} != buffer_set.vertex_buffers[i]) {
            return false;
        }
    }
    entries.emplace_back(&mesh, static_cast<uint16_t>(mesh_primitive_index));
    return true;
}

void bucket_primitives(
    std::vector<Render_bucket>&                                buckets,
    const uint32_t                                             boolean_mask_force_enable,
    const uint32_t                                             boolean_mask_force_disable,
    const Mesh_memory&                                         mesh_memory,
    const Shader_key&                                          environment_shader_key,
    const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
    const erhe::Item_filter&                                   filter,
    const erhe::primitive::Primitive_mode                      primitive_mode,
    const Blending_mode_policy                                 blending_mode_policy
)
{
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
        const auto primitives = mesh->get_primitives();
        if (!filter(mesh->get_flag_bits())) {
            // log_draw->warn("filtered away {} filter: {}", mesh->describe(2), filter.describe());
            // static_cast<void>(filter(mesh->get_flag_bits()));
            continue;
        }

        for (size_t i = 0, count = primitives.size(); i < count; ++i) {
            const erhe::scene::Mesh_primitive& mesh_primitive = primitives[i];
            const erhe::primitive::Primitive* primitive = mesh_primitive.primitive.get();
            if (primitive == nullptr) {
                continue;
            }
            const erhe::primitive::Buffer_mesh* buffer_mesh = primitive->get_renderable_mesh();
            if (buffer_mesh == nullptr) {
                continue;
            }
            if (buffer_mesh->index_range(primitive_mode).index_count == 0) {
                continue;
            }

            erhe::primitive::Material* material = mesh_primitive.material.get();

            const Vertex_input_entry& vertex_input_entry = mesh_memory.get_vertex_input(buffer_mesh->vertex_input_key);

            Shader_key shader_key = environment_shader_key.derive(
                material,
                &vertex_input_entry.vertex_format,
                static_cast<bool>(mesh->skin)
            );
            shader_key.bool_mask |=  boolean_mask_force_enable;
            shader_key.bool_mask &= ~boolean_mask_force_disable;
            switch (blending_mode_policy) {
                case Blending_mode_policy::not_set: {
                    ERHE_FATAL("Blending_mode_policy::not_set");
                    break;
                }
                case Blending_mode_policy::opaque_primitives_only: {
                    if (shader_key.blending_mode != erhe::primitive::Material_blending_mode::opaque) {
                        continue;
                    }
                    break;
                }
                case Blending_mode_policy::translucent_primitives_only: {
                    if (shader_key.blending_mode == erhe::primitive::Material_blending_mode::opaque) {
                        continue;
                    }
                    break;
                }
                case Blending_mode_policy::allow_all: {
                    // NOP
                    break;
                }
                case Blending_mode_policy::override_with_base_render_pipeline: {
                    erhe::primitive::Material_blending_mode blend_override = environment_shader_key.blending_mode.has_value() 
                        ? environment_shader_key.blending_mode.value()
                        : erhe::primitive::Material_blending_mode::opaque;
                    if (shader_key.blending_mode.has_value()) {
                        if (shader_key.blending_mode.value() != blend_override) {
                            log_draw->warn(
                                "Overriding blending mode for {} from {} to {}",
                                mesh->describe(2),
                                erhe::primitive::c_str(shader_key.blending_mode.value()),
                                erhe::primitive::c_str(blend_override)
                            );
                        }
                    }
                    shader_key.blending_mode = blend_override;
                    break;
                }
                default: {
                    ERHE_FATAL("Bad Blending_mode_policy");
                    break;
                }
            }
            const uint64_t shader_key_hash = shader_key.get_hash();

            bool done = false;
            for (Render_bucket& b : buckets) {
                if (b.accept(*mesh.get(), i, *buffer_mesh, shader_key_hash)) {
                    done = true;
                    break;
                }
            }
            if (done) {
                continue;
            }

            buckets.emplace_back(*mesh.get(), i, *buffer_mesh, shader_key, shader_key_hash);
        }
    }
}


}
