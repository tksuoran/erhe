#include "erhe_graphics/metal4/metal4_indirect_count_draws.hpp"
#include "erhe_graphics/metal4/metal4_pipeline_compiler.hpp"
#include "erhe_graphics/metal4/metal4_buffer.hpp"
#include "erhe_graphics/metal4/metal4_device.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <bit>
#include <cstring>

namespace erhe::graphics {

namespace {

// One thread per (command slot, job): thread (slot, job) encodes the job's
// command slot into the ICB entry of the same index when slot < count, and
// slot 0 writes the job's execution range {command_offset, count}. Entries
// at or past a job's count are never executed - the range excludes them -
// so stale entries from an earlier frame need no reset(). The commands keep
// the encoder's pipeline and buffer bindings (inheritPipelineState,
// inheritBuffers); the index buffer is the one argument a draw command
// carries, as a typed pointer advanced by first_index. base_vertex is uint
// in MSL where Vulkan's vertexOffset is int32: the 32-bit wrap of the
// vertex index gives the same result for a negative offset.
constexpr const char* c_indirect_count_encode_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct Icb_argument
{
    command_buffer commands;
};

struct Draw_command
{
    uint index_count;
    uint instance_count;
    uint first_index;
    uint base_vertex;
    uint base_instance;
};

struct Job
{
    uint command_offset;
    uint max_draw_count;
    uint index_buffer;
    uint reserved;
};

struct Index_buffer
{
    device const uchar* address;
    uint                index_size;
    uint                reserved;
};

struct Execution_range
{
    uint location;
    uint length;
};

struct Params
{
    uint job_count;
    uint count_stride_words;
    uint primitive_type;
    uint reserved;
};

kernel void erhe_encode_indexed_indirect_count_draws(
    device const Icb_argument& icb           [[buffer(0)]],
    device const Draw_command* commands      [[buffer(1)]],
    device const uint*         counts        [[buffer(2)]],
    device const Job*          jobs          [[buffer(3)]],
    device const Index_buffer* index_buffers [[buffer(4)]],
    device Execution_range*    ranges        [[buffer(5)]],
    constant Params&           params        [[buffer(6)]],
    uint2                      position      [[thread_position_in_grid]]
)
{
    const uint slot      = position.x;
    const uint job_index = position.y;
    if (job_index >= params.job_count) {
        return;
    }
    const Job  job   = jobs[job_index];
    const uint count = min(counts[job_index * params.count_stride_words], job.max_draw_count);
    if (slot == 0u) {
        ranges[job_index] = Execution_range{job.command_offset, count};
    }
    if (slot >= count) {
        return;
    }
    const uint           command_index = job.command_offset + slot;
    const Draw_command   command       = commands[command_index];
    const Index_buffer   index_buffer  = index_buffers[job.index_buffer];
    const primitive_type type          = static_cast<primitive_type>(params.primitive_type);
    render_command draw(icb.commands, command_index);
    if (index_buffer.index_size == 2u) {
        device const ushort* indices = reinterpret_cast<device const ushort*>(index_buffer.address) + command.first_index;
        draw.draw_indexed_primitives(type, command.index_count, indices, command.instance_count, command.base_vertex, command.base_instance);
    } else {
        device const uint* indices = reinterpret_cast<device const uint*>(index_buffer.address) + command.first_index;
        draw.draw_indexed_primitives(type, command.index_count, indices, command.instance_count, command.base_vertex, command.base_instance);
    }
}
)";

// The CPU-written table buffer: the ICB argument (its MTLResourceID), then
// the index buffers, then the jobs (the kernel's structs above).
class Gpu_index_buffer
{
public:
    uint64_t address;
    uint32_t index_size;
    uint32_t reserved;
};
static_assert(sizeof(Gpu_index_buffer) == 16);

class Gpu_job
{
public:
    uint32_t command_offset;
    uint32_t max_draw_count;
    uint32_t index_buffer;
    uint32_t reserved;
};
static_assert(sizeof(Gpu_job) == 16);

class Gpu_params
{
public:
    uint32_t job_count;
    uint32_t count_stride_words;
    uint32_t primitive_type;
    uint32_t reserved;
};

constexpr std::size_t c_icb_argument_bytes = 16; // MTLResourceID (8 bytes), padded
constexpr std::size_t c_min_command_count  = 64;
constexpr std::size_t c_command_count_granularity = 1024;
constexpr std::size_t c_min_job_count      = 16;
constexpr std::size_t c_min_table_bytes    = 256;
// Each prepare's table range starts at this alignment (buffer offsets).
constexpr std::size_t c_table_alignment    = 256;

[[nodiscard]] auto to_mtl_primitive_type_value(const Primitive_type type) -> uint32_t
{
    switch (type) {
        case Primitive_type::point:          return static_cast<uint32_t>(MTL::PrimitiveTypePoint);
        case Primitive_type::line:           return static_cast<uint32_t>(MTL::PrimitiveTypeLine);
        case Primitive_type::line_strip:     return static_cast<uint32_t>(MTL::PrimitiveTypeLineStrip);
        case Primitive_type::triangle:       return static_cast<uint32_t>(MTL::PrimitiveTypeTriangle);
        case Primitive_type::triangle_strip: return static_cast<uint32_t>(MTL::PrimitiveTypeTriangleStrip);
        default: {
            ERHE_FATAL("Indirect_count_draws: unsupported primitive type %u", static_cast<unsigned int>(type));
        }
    }
}

[[nodiscard]] auto index_size_of(const erhe::dataformat::Format format) -> uint32_t
{
    switch (format) {
        case erhe::dataformat::Format::format_16_scalar_uint: return 2;
        case erhe::dataformat::Format::format_32_scalar_uint: return 4;
        default: {
            ERHE_FATAL("Indirect_count_draws: unsupported index type");
        }
    }
}

// Replaced (grown) and destroyed resources are released through the
// device's completion handlers, as the other Metal resource releases are, so
// no frame still in flight loses one; they leave the residency set then.
void release_later(Device& device, MTL::Resource* const resource)
{
    if (resource == nullptr) {
        return;
    }
    device.get_impl().add_completion_handler(
        [resource](Device_impl& device_impl) {
            device_impl.get_residency().remove(resource);
            resource->release();
        }
    );
}

void make_resident(Device& device, const MTL::Resource* const resource)
{
    device.get_impl().get_residency().add(resource);
}

} // anonymous namespace

auto make_indirect_count_encode_pipeline(Pipeline_compiler& pipeline_compiler) -> MTL::ComputePipelineState*
{
    std::string error;
    MTL::ComputePipelineState* const pipeline = pipeline_compiler.new_compute_pipeline_from_source(
        c_indirect_count_encode_msl,
        "erhe_encode_indexed_indirect_count_draws",
        "indirect_count_draws_encode",
        error
    );
    if (pipeline == nullptr) {
        log_startup->error("Indirect count draws: kernel pipeline failed: {}", error);
    }
    return pipeline;
}

Indirect_count_draws_impl::Indirect_count_draws_impl(Device& device, const erhe::utility::Debug_label& debug_label)
    : m_device     {device}
    , m_debug_label{debug_label}
    , m_slots      (device.get_number_of_frames_in_flight())
{
}

Indirect_count_draws_impl::~Indirect_count_draws_impl() noexcept
{
    for (Slot_resources& resources : m_slots) {
        release_later(m_device, resources.icb);
        release_later(m_device, resources.range_buffer);
        release_later(m_device, resources.table_buffer);
        resources = Slot_resources{};
    }
}

void Indirect_count_draws_impl::reserve(
    Slot_resources&   resources,
    const std::size_t command_count,
    const std::size_t job_count
)
{
    MTL::Device* const mtl_device = m_device.get_impl().get_mtl_device();
    ERHE_VERIFY(mtl_device != nullptr);

    if ((resources.icb == nullptr) || (resources.icb_capacity < command_count)) {
        // Commands cost about 692 bytes each, so the ICB grows in steps of
        // c_command_count_granularity rather than doubling.
        const std::size_t wanted   = std::max(command_count, c_min_command_count);
        const std::size_t capacity = ((wanted + c_command_count_granularity - 1) / c_command_count_granularity) * c_command_count_granularity;
        release_later(m_device, resources.icb);
        MTL::IndirectCommandBufferDescriptor* const descriptor = MTL::IndirectCommandBufferDescriptor::alloc()->init();
        descriptor->setCommandTypes(MTL::IndirectCommandTypeDrawIndexed);
        // The commands draw with the encoder's pipeline, depth stencil, cull
        // and winding state and buffer bindings (the inheritance of the
        // macOS 26 states defaults to true); they carry only the draw.
        descriptor->setInheritPipelineState(true);
        descriptor->setInheritBuffers(true);
        descriptor->setMaxVertexBufferBindCount(0);
        descriptor->setMaxFragmentBufferBindCount(0);
        resources.icb = mtl_device->newIndirectCommandBuffer(descriptor, static_cast<NS::UInteger>(capacity), MTL::ResourceStorageModePrivate);
        descriptor->release();
        if (resources.icb == nullptr) {
            ERHE_FATAL("Indirect_count_draws '%s': newIndirectCommandBuffer(%zu commands) failed", m_debug_label.data(), capacity);
        }
        resources.icb_capacity = capacity;
        make_resident(m_device, resources.icb);
        log_startup->info(
            "Indirect_count_draws '{}': indirect command buffer grown to {} commands ({} bytes)",
            m_debug_label.string_view(), capacity, resources.icb->allocatedSize()
        );
    }
    if ((resources.range_buffer == nullptr) || (resources.range_capacity < job_count)) {
        const std::size_t capacity = std::bit_ceil(std::max(job_count, c_min_job_count));
        release_later(m_device, resources.range_buffer);
        resources.range_buffer = mtl_device->newBuffer(
            static_cast<NS::UInteger>(capacity * sizeof(MTL::IndirectCommandBufferExecutionRange)),
            MTL::ResourceStorageModePrivate
        );
        ERHE_VERIFY(resources.range_buffer != nullptr);
        resources.range_capacity = capacity;
        make_resident(m_device, resources.range_buffer);
        log_startup->info("Indirect_count_draws '{}': range buffer grown to {} jobs", m_debug_label.string_view(), capacity);
    }
}

auto Indirect_count_draws_impl::allocate_table(
    Slot_resources&   resources,
    const uint64_t    frame_index,
    const std::size_t table_bytes
) -> std::size_t
{
    // Each prepare's table is a range of the slot's table buffer, used
    // linearly through the frame: the slot's previous frame retired before
    // this frame acquired the slot, so its ranges are free.
    if (resources.table_frame != frame_index) {
        resources.table_frame = frame_index;
        resources.table_used  = 0;
    }
    const std::size_t offset = ((resources.table_used + c_table_alignment - 1) / c_table_alignment) * c_table_alignment;
    if ((resources.table_buffer != nullptr) && ((offset + table_bytes) <= resources.table_capacity)) {
        resources.table_used = offset + table_bytes;
        return offset;
    }
    // Grow: the frame's earlier tables stay in the old buffer (released
    // once the frame completed); this one starts the new buffer.
    MTL::Device* const mtl_device = m_device.get_impl().get_mtl_device();
    ERHE_VERIFY(mtl_device != nullptr);
    const std::size_t capacity = std::bit_ceil(std::max({offset + table_bytes, table_bytes, c_min_table_bytes}));
    release_later(m_device, resources.table_buffer);
    resources.table_buffer = mtl_device->newBuffer(
        static_cast<NS::UInteger>(capacity),
        MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined
    );
    ERHE_VERIFY(resources.table_buffer != nullptr);
    resources.table_capacity = capacity;
    make_resident(m_device, resources.table_buffer);
    resources.table_used     = table_bytes;
    log_startup->info("Indirect_count_draws '{}': table buffer grown to {} bytes", m_debug_label.string_view(), capacity);
    return 0;
}

void Indirect_count_draws_impl::encode(
    MTL4::ComputeCommandEncoder* const encoder,
    MTL4::ArgumentTable* const         argument_table,
    const Indirect_count_draws::Slot&  slot,
    const std::size_t                  slot_index
)
{
    ERHE_VERIFY(encoder != nullptr);
    ERHE_VERIFY(argument_table != nullptr);
    ERHE_VERIFY(slot_index < m_slots.size());
    MTL::ComputePipelineState* const pipeline = m_device.get_impl().get_indirect_count_encode_pipeline();
    ERHE_VERIFY(pipeline != nullptr);

    Slot_resources& resources = m_slots[slot_index];

    std::size_t command_count  = 0;
    uint32_t    max_draw_count = 0;
    for (const Indirect_count_draw_job& job : slot.jobs) {
        command_count  = std::max(command_count, static_cast<std::size_t>(job.command_offset) + static_cast<std::size_t>(job.max_draw_count));
        max_draw_count = std::max(max_draw_count, job.max_draw_count);
    }
    const std::size_t job_count          = slot.jobs.size();
    const std::size_t index_buffer_bytes = slot.index_buffers.size() * sizeof(Gpu_index_buffer);
    const std::size_t job_bytes          = job_count * sizeof(Gpu_job);
    reserve(resources, command_count, job_count);
    resources.encoded_sequence = slot.sequence;
    if (job_count == 0) {
        return;
    }

    // The table: written by the CPU into this prepare's range of the slot's
    // shared table buffer; the kernel parameters last (they were setBytes
    // in the Metal 3 backend).
    const std::size_t params_bytes       = sizeof(Gpu_params);
    const std::size_t table_bytes        = c_icb_argument_bytes + index_buffer_bytes + job_bytes + params_bytes;
    const std::size_t table_offset       = allocate_table(resources, slot.frame_index, table_bytes);
    const std::size_t index_table_offset = table_offset + c_icb_argument_bytes;
    const std::size_t job_table_offset   = index_table_offset + index_buffer_bytes;
    const std::size_t params_offset      = job_table_offset + job_bytes;
    std::byte* const table = static_cast<std::byte*>(resources.table_buffer->contents()) + table_offset;
    const MTL::ResourceID icb_id = resources.icb->gpuResourceID();
    static_assert(sizeof(MTL::ResourceID) <= c_icb_argument_bytes);
    std::memcpy(table, &icb_id, sizeof(icb_id));
    for (std::size_t i = 0, end = slot.index_buffers.size(); i < end; ++i) {
        const Indirect_count_index_buffer& index_buffer = slot.index_buffers[i];
        MTL::Buffer* const mtl_buffer = index_buffer.buffer->get_impl().get_mtl_buffer();
        ERHE_VERIFY(mtl_buffer != nullptr);
        const Gpu_index_buffer entry{
            .address    = mtl_buffer->gpuAddress(),
            .index_size = index_size_of(index_buffer.index_type),
            .reserved   = 0
        };
        std::memcpy(table + c_icb_argument_bytes + (i * sizeof(Gpu_index_buffer)), &entry, sizeof(entry));
    }
    for (std::size_t i = 0; i < job_count; ++i) {
        const Indirect_count_draw_job& job = slot.jobs[i];
        const Gpu_job entry{
            .command_offset = job.command_offset,
            .max_draw_count = job.max_draw_count,
            .index_buffer   = job.index_buffer,
            .reserved       = 0
        };
        std::memcpy(table + c_icb_argument_bytes + index_buffer_bytes + (i * sizeof(Gpu_job)), &entry, sizeof(entry));
    }
    const Gpu_params params{
        .job_count          = static_cast<uint32_t>(job_count),
        .count_stride_words = static_cast<uint32_t>(slot.count_byte_stride / sizeof(uint32_t)),
        .primitive_type     = to_mtl_primitive_type_value(slot.primitive_type),
        .reserved           = 0
    };
    std::memcpy(static_cast<std::byte*>(resources.table_buffer->contents()) + params_offset, &params, sizeof(params));

    MTL::Buffer* const command_buffer = slot.command_buffer->get_impl().get_mtl_buffer();
    MTL::Buffer* const count_buffer   = slot.count_buffer->get_impl().get_mtl_buffer();
    ERHE_VERIFY((command_buffer != nullptr) && (count_buffer != nullptr));

    const uint64_t table_address = resources.table_buffer->gpuAddress();
    encoder->setComputePipelineState(pipeline);
    argument_table->setAddress(table_address + table_offset,                            0);
    argument_table->setAddress(command_buffer->gpuAddress() + slot.command_byte_offset, 1);
    argument_table->setAddress(count_buffer->gpuAddress() + slot.count_byte_offset,     2);
    argument_table->setAddress(table_address + job_table_offset,                        3);
    argument_table->setAddress(table_address + index_table_offset,                      4);
    argument_table->setAddress(resources.range_buffer->gpuAddress(),                    5);
    argument_table->setAddress(table_address + params_offset,                           6);
    // The ICB is reached through the table (by resource id), not bound; it
    // is resident through the device's residency set.

    // Slot 0 of every job runs (it writes the range), so the grid is at
    // least one slot wide.
    const NS::UInteger grid_width  = static_cast<NS::UInteger>(std::max<uint32_t>(max_draw_count, 1u));
    const NS::UInteger group_width = std::min<NS::UInteger>(grid_width, pipeline->threadExecutionWidth());
    encoder->dispatchThreads(
        MTL::Size(grid_width, static_cast<NS::UInteger>(job_count), 1),
        MTL::Size(group_width, 1, 1)
    );
}

auto Indirect_count_draws_impl::get_slot_resources(const std::size_t slot_index) const -> const Slot_resources&
{
    ERHE_VERIFY(slot_index < m_slots.size());
    return m_slots[slot_index];
}

} // namespace erhe::graphics
