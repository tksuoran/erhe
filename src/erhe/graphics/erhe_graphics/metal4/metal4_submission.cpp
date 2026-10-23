#include "erhe_graphics/metal4/metal4_submission.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <bit>
#include <cstring>

namespace erhe::graphics {

namespace {

static_assert(c_stage_vertex                 == static_cast<std::uint64_t>(MTL::StageVertex));
static_assert(c_stage_fragment               == static_cast<std::uint64_t>(MTL::StageFragment));
static_assert(c_stage_tile                   == static_cast<std::uint64_t>(MTL::StageTile));
static_assert(c_stage_object                 == static_cast<std::uint64_t>(MTL::StageObject));
static_assert(c_stage_mesh                   == static_cast<std::uint64_t>(MTL::StageMesh));
static_assert(c_stage_dispatch               == static_cast<std::uint64_t>(MTL::StageDispatch));
static_assert(c_stage_blit                   == static_cast<std::uint64_t>(MTL::StageBlit));
static_assert(c_stage_acceleration_structure == static_cast<std::uint64_t>(MTL::StageAccelerationStructure));

constexpr std::size_t c_initial_draw_id_count = 4096;

} // anonymous namespace

namespace {

// The MTL::Stages of the shader accesses of one barrier side: the stages its
// vertex_shader / fragment_shader / compute_shader qualifiers name, every
// shader stage without one. Image and buffer qualifiers share their bits.
[[nodiscard]] auto get_barrier_shader_stages(const std::uint64_t usage) -> std::uint64_t
{
    static_assert(Image_usage_flag_bit_mask::vertex_shader   == Buffer_usage_flag_bit_mask::vertex_shader);
    static_assert(Image_usage_flag_bit_mask::fragment_shader == Buffer_usage_flag_bit_mask::fragment_shader);
    static_assert(Image_usage_flag_bit_mask::compute_shader  == Buffer_usage_flag_bit_mask::compute_shader);
    if ((usage & Image_usage_flag_bit_mask::shader_stages) == 0) {
        return MTL::StageVertex | MTL::StageFragment | MTL::StageDispatch;
    }
    std::uint64_t stages = 0;
    if ((usage & Image_usage_flag_bit_mask::vertex_shader) != 0) {
        stages |= MTL::StageVertex;
    }
    if ((usage & Image_usage_flag_bit_mask::fragment_shader) != 0) {
        stages |= MTL::StageFragment;
    }
    if ((usage & Image_usage_flag_bit_mask::compute_shader) != 0) {
        stages |= MTL::StageDispatch;
    }
    return stages;
}

} // anonymous namespace

auto get_image_usage_stages(const std::uint64_t usage) -> std::uint64_t
{
    std::uint64_t stages = 0;
    if ((usage & (Image_usage_flag_bit_mask::transfer_src | Image_usage_flag_bit_mask::transfer_dst)) != 0) {
        stages |= MTL::StageBlit;
    }
    if ((usage & (Image_usage_flag_bit_mask::sampled | Image_usage_flag_bit_mask::storage | Image_usage_flag_bit_mask::input_attachment)) != 0) {
        stages |= get_barrier_shader_stages(usage);
    }
    if ((usage & (Image_usage_flag_bit_mask::color_attachment | Image_usage_flag_bit_mask::depth_stencil_attachment)) != 0) {
        // Attachment loads, depth / stencil tests, stores and resolves all
        // happen in the fragment stage of the pass (tile memory loads at its
        // start, stores and resolves at its end), so the fragment stage is the
        // producer and the consumer stage of an attachment access.
        stages |= MTL::StageFragment;
    }
    // none, present (the drawable is ordered by the queue's drawable wait),
    // host_transfer and fragment_density_map are no GPU stage of this queue;
    // discard changes nothing here (no layouts).
    return stages;
}

auto get_buffer_usage_stages(const std::uint64_t usage) -> std::uint64_t
{
    std::uint64_t stages = 0;
    if ((usage & (Buffer_usage_flag_bit_mask::vertex | Buffer_usage_flag_bit_mask::index)) != 0) {
        stages |= MTL::StageVertex;
    }
    if ((usage & Buffer_usage_flag_bit_mask::indirect) != 0) {
        // Draw arguments (and indirect command buffer ranges) are read at the
        // vertex stage, dispatch arguments at the dispatch stage; like Vulkan's
        // DRAW_INDIRECT, the access is not qualified by a shader stage.
        stages |= MTL::StageVertex | MTL::StageDispatch;
    }
    const std::uint64_t shader_accesses =
        Buffer_usage_flag_bit_mask::uniform       |
        Buffer_usage_flag_bit_mask::storage_read  |
        Buffer_usage_flag_bit_mask::storage_write |
        Buffer_usage_flag_bit_mask::acceleration_structure_read;
    if ((usage & shader_accesses) != 0) {
        stages |= get_barrier_shader_stages(usage);
    }
    if ((usage & (Buffer_usage_flag_bit_mask::transfer_src | Buffer_usage_flag_bit_mask::transfer_dst)) != 0) {
        stages |= MTL::StageBlit;
    }
    const std::uint64_t build_accesses =
        Buffer_usage_flag_bit_mask::acceleration_structure_build_input |
        Buffer_usage_flag_bit_mask::acceleration_structure_read        |
        Buffer_usage_flag_bit_mask::acceleration_structure_write;
    if ((usage & build_accesses) != 0) {
        stages |= MTL::StageAccelerationStructure;
    }
    // host_read / host_write: ordered by command buffer completion and commit.
    return stages;
}

auto get_image_layout_stages(const Image_layout layout) -> std::uint64_t
{
    // The accesses a texture in the layout takes part in, in the image usage
    // vocabulary (one table: get_image_usage_stages()). general may be any
    // access; present, undefined and the fragment density map are no GPU
    // stage of this queue.
    switch (layout) {
        case Image_layout::shader_read_only_optimal:         return get_image_usage_stages(Image_usage_flag_bit_mask::sampled);
        case Image_layout::depth_stencil_read_only_optimal:  return get_image_usage_stages(Image_usage_flag_bit_mask::sampled | Image_usage_flag_bit_mask::depth_stencil_attachment);
        case Image_layout::transfer_dst_optimal:             return get_image_usage_stages(Image_usage_flag_bit_mask::transfer_dst);
        case Image_layout::transfer_src_optimal:             return get_image_usage_stages(Image_usage_flag_bit_mask::transfer_src);
        case Image_layout::color_attachment_optimal:         return get_image_usage_stages(Image_usage_flag_bit_mask::color_attachment);
        case Image_layout::depth_stencil_attachment_optimal: return get_image_usage_stages(Image_usage_flag_bit_mask::depth_stencil_attachment);
        case Image_layout::general:                          return c_all_stages;
        case Image_layout::undefined:
        case Image_layout::fragment_density_map_optimal:
        case Image_layout::present_src:
        default:                                             return 0;
    }
}

auto get_memory_barrier_stages(const Memory_barrier_mask mask) -> std::uint64_t
{
    // The destination stages of Vulkan's memory_barrier
    // (vulkan_command_buffer.cpp), in the buffer and image usage vocabulary.
    const unsigned int bits = static_cast<unsigned int>(mask);
    const auto has = [bits](const Memory_barrier_mask bit) -> bool {
        return (bits & static_cast<unsigned int>(bit)) != 0;
    };
    std::uint64_t stages = 0;
    if (has(Memory_barrier_mask::vertex_attrib_array_barrier_bit)) {
        stages |= get_buffer_usage_stages(Buffer_usage_flag_bit_mask::vertex);
    }
    if (has(Memory_barrier_mask::element_array_barrier_bit)) {
        stages |= get_buffer_usage_stages(Buffer_usage_flag_bit_mask::index);
    }
    if (has(Memory_barrier_mask::uniform_barrier_bit)) {
        stages |= get_buffer_usage_stages(Buffer_usage_flag_bit_mask::uniform);
    }
    if (has(Memory_barrier_mask::texture_fetch_barrier_bit)) {
        stages |= get_image_usage_stages(Image_usage_flag_bit_mask::sampled);
    }
    if (has(Memory_barrier_mask::shader_image_access_barrier_bit)) {
        stages |= get_image_usage_stages(Image_usage_flag_bit_mask::storage);
    }
    if (has(Memory_barrier_mask::atomic_counter_barrier_bit) || has(Memory_barrier_mask::shader_storage_barrier_bit)) {
        stages |= get_buffer_usage_stages(Buffer_usage_flag_bit_mask::storage_read | Buffer_usage_flag_bit_mask::storage_write);
    }
    if (has(Memory_barrier_mask::command_barrier_bit)) {
        stages |= get_buffer_usage_stages(Buffer_usage_flag_bit_mask::indirect);
    }
    if (
        has(Memory_barrier_mask::pixel_buffer_barrier_bit)   ||
        has(Memory_barrier_mask::texture_update_barrier_bit) ||
        has(Memory_barrier_mask::buffer_update_barrier_bit)
    ) {
        stages |= get_buffer_usage_stages(Buffer_usage_flag_bit_mask::transfer_src | Buffer_usage_flag_bit_mask::transfer_dst);
    }
    if (has(Memory_barrier_mask::framebuffer_barrier_bit)) {
        stages |= get_image_usage_stages(Image_usage_flag_bit_mask::color_attachment | Image_usage_flag_bit_mask::depth_stencil_attachment);
    }
    // client_mapped_buffer_barrier_bit: a host read, no GPU stage; shared
    // memory written by the GPU is visible to the CPU once the commit has
    // completed, which is what the reader waits for.
    return stages;
}

void encode_consumer_barrier(MTL4::CommandEncoder* const encoder, const std::uint64_t after_stages, const std::uint64_t before_stages)
{
    ERHE_VERIFY(encoder != nullptr);
    encoder->barrierAfterQueueStages(
        static_cast<MTL::Stages>(after_stages),
        static_cast<MTL::Stages>(before_stages),
        MTL4::VisibilityOptionDevice
    );
}

void encode_producer_barrier(MTL4::CommandEncoder* const encoder, const std::uint64_t after_stages, const std::uint64_t before_stages)
{
    ERHE_VERIFY(encoder != nullptr);
    encoder->barrierAfterStages(
        static_cast<MTL::Stages>(after_stages),
        static_cast<MTL::Stages>(before_stages),
        MTL4::VisibilityOptionDevice
    );
}

void encode_intra_encoder_barrier(MTL4::ComputeCommandEncoder* const encoder)
{
    ERHE_VERIFY(encoder != nullptr);
    encoder->barrierAfterEncoderStages(static_cast<MTL::Stages>(c_compute_encoder_stages), static_cast<MTL::Stages>(c_compute_encoder_stages), MTL4::VisibilityOptionDevice);
}

Residency::Residency(MTL::Device* const mtl_device)
{
    if (mtl_device == nullptr) {
        return;
    }
    Scoped_transient_object_pool object_pool{};
    MTL::ResidencySetDescriptor* const descriptor = MTL::ResidencySetDescriptor::alloc()->init();
    descriptor->setLabel(NS::String::string("erhe residency set", NS::UTF8StringEncoding));
    descriptor->setInitialCapacity(4096);
    NS::Error* error = nullptr;
    m_residency_set = mtl_device->newResidencySet(descriptor, &error);
    descriptor->release();
    if (m_residency_set == nullptr) {
        ERHE_FATAL(
            "Metal 4: newResidencySet failed: %s",
            (error != nullptr) ? error->localizedDescription()->utf8String() : "(no error object)"
        );
    }
    m_removed.reserve(256);
}

Residency::~Residency() noexcept
{
    for (MTL::Allocation* const allocation : m_removed) {
        allocation->release();
    }
    m_removed.clear();
    if (m_residency_set != nullptr) {
        m_residency_set->removeAllAllocations();
        m_residency_set->commit();
        m_residency_set->release();
        m_residency_set = nullptr;
    }
}

auto Residency::get_residency_set() const -> MTL::ResidencySet*
{
    return m_residency_set;
}

void Residency::add(const MTL::Allocation* const allocation)
{
    if (allocation == nullptr) {
        return;
    }
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_residency_set->addAllocation(allocation);
    m_changed = true;
}

void Residency::remove(MTL::Allocation* const allocation)
{
    if (allocation == nullptr) {
        return;
    }
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_residency_set->removeAllocation(allocation);
    allocation->retain();
    m_removed.push_back(allocation);
    m_changed = true;
}

void Residency::commit_if_changed()
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_changed) {
        return;
    }
    m_residency_set->commit();
    m_changed = false;
    for (MTL::Allocation* const allocation : m_removed) {
        allocation->release();
    }
    m_removed.clear();
}

Recording::~Recording() noexcept
{
    if (command_buffer != nullptr) {
        command_buffer->release();
    }
    if (allocator != nullptr) {
        allocator->release();
    }
}

Submission_pools::Submission_pools(MTL::Device* const mtl_device, Residency& residency)
    : m_mtl_device {mtl_device}
    , m_residency  {residency}
{
    m_all_recordings.reserve(64);
    m_free_recordings.reserve(64);
    m_free_argument_tables.reserve(256);
    m_free_inline_bytes_chunks.reserve(64);
    m_all_inline_bytes_chunks.reserve(64);
    if (!create_draw_id_table(c_initial_draw_id_count)) {
        ERHE_FATAL("Metal 4: could not create the draw id table");
    }
    m_draw_id_zero = m_draw_id_tables.front()->gpuAddress();
}

Submission_pools::~Submission_pools() noexcept
{
    // Every recording is back: the device waited for all commit feedback.
    ERHE_VERIFY(m_free_recordings.size() == m_all_recordings.size());
    m_free_recordings.clear();
    m_all_recordings.clear();
    for (MTL4::ArgumentTable* const argument_table : m_free_argument_tables) {
        argument_table->release();
    }
    m_free_argument_tables.clear();
    for (MTL::Buffer* const chunk : m_all_inline_bytes_chunks) {
        m_residency.remove(chunk);
        chunk->release();
    }
    m_all_inline_bytes_chunks.clear();
    m_free_inline_bytes_chunks.clear();
    for (MTL::Buffer* const table : m_draw_id_tables) {
        m_residency.remove(table);
        table->release();
    }
    m_draw_id_tables.clear();
}

auto Submission_pools::acquire_recording() -> Recording*
{
    Recording* recording = nullptr;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_free_recordings.empty()) {
            recording = m_free_recordings.back();
            m_free_recordings.pop_back();
        }
    }
    if (recording == nullptr) {
        Scoped_transient_object_pool object_pool{};
        std::unique_ptr<Recording> new_recording = std::make_unique<Recording>();
        new_recording->command_buffer = m_mtl_device->newCommandBuffer();
        new_recording->allocator      = m_mtl_device->newCommandAllocator();
        ERHE_VERIFY((new_recording->command_buffer != nullptr) && (new_recording->allocator != nullptr));
        Recording* const raw = new_recording.get();
        recording = raw;
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_all_recordings.push_back(std::move(new_recording));
    }
    recording->command_buffer->beginCommandBuffer(recording->allocator);
    return recording;
}

void Submission_pools::release_recording(Recording* const recording)
{
    ERHE_VERIFY(recording != nullptr);
    ERHE_VERIFY(recording->argument_tables.empty() && recording->inline_bytes_chunks.empty());
    ERHE_VERIFY(recording->retained_objects.empty() && recording->callbacks.empty());
    recording->allocator->reset();
    recording->gpu_timer_samples = Gpu_timer_command_buffer_samples{};
    recording->serial           = 0;
    recording->counts_frame     = false;
    recording->frame_index      = 0;
    recording->presents           = false;
    recording->present_prefix     = false;
    recording->has_present_prefix = false;
    recording->present_frame_id   = 0;
    recording->on_feedback      = nullptr;
    recording->user             = nullptr;
    recording->user_result      = nullptr;
    recording->user_value       = 0;
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_free_recordings.push_back(recording);
}

auto Submission_pools::acquire_argument_table() -> MTL4::ArgumentTable*
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_free_argument_tables.empty()) {
            MTL4::ArgumentTable* const argument_table = m_free_argument_tables.back();
            m_free_argument_tables.pop_back();
            return argument_table;
        }
    }
    Scoped_transient_object_pool object_pool{};
    MTL4::ArgumentTableDescriptor* const descriptor = MTL4::ArgumentTableDescriptor::alloc()->init();
    descriptor->setMaxBufferBindCount      (c_argument_table_buffer_count);
    descriptor->setMaxTextureBindCount     (c_argument_table_texture_count);
    descriptor->setMaxSamplerStateBindCount(c_argument_table_sampler_count);
    descriptor->setInitializeBindings      (true);
    descriptor->setSupportAttributeStrides (false);
    NS::Error* error = nullptr;
    MTL4::ArgumentTable* const argument_table = m_mtl_device->newArgumentTable(descriptor, &error);
    descriptor->release();
    if (argument_table == nullptr) {
        ERHE_FATAL(
            "Metal 4: newArgumentTable failed: %s",
            (error != nullptr) ? error->localizedDescription()->utf8String() : "(no error object)"
        );
    }
    return argument_table;
}

void Submission_pools::release_argument_table(MTL4::ArgumentTable* const argument_table)
{
    if (argument_table == nullptr) {
        return;
    }
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_free_argument_tables.push_back(argument_table);
}

auto Submission_pools::acquire_inline_bytes_chunk() -> MTL::Buffer*
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_free_inline_bytes_chunks.empty()) {
            MTL::Buffer* const chunk = m_free_inline_bytes_chunks.back();
            m_free_inline_bytes_chunks.pop_back();
            return chunk;
        }
    }
    MTL::Buffer* const chunk = m_mtl_device->newBuffer(c_inline_bytes_chunk_size, MTL::ResourceStorageModeShared);
    ERHE_VERIFY(chunk != nullptr);
    {
        Scoped_transient_object_pool object_pool{};
        chunk->setLabel(NS::String::string("erhe inline bytes", NS::UTF8StringEncoding));
    }
    m_residency.add(chunk);
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_all_inline_bytes_chunks.push_back(chunk);
    return chunk;
}

void Submission_pools::release_inline_bytes_chunk(MTL::Buffer* const chunk)
{
    if (chunk == nullptr) {
        return;
    }
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_free_inline_bytes_chunks.push_back(chunk);
}

auto Submission_pools::create_draw_id_table(const std::size_t entry_count) -> bool
{
    MTL::Buffer* const table = m_mtl_device->newBuffer(entry_count * c_draw_id_stride, MTL::ResourceStorageModeShared);
    if (table == nullptr) {
        return false;
    }
    {
        Scoped_transient_object_pool object_pool{};
        table->setLabel(NS::String::string("erhe draw id table", NS::UTF8StringEncoding));
    }
    std::byte* const contents = static_cast<std::byte*>(table->contents());
    std::memset(contents, 0, entry_count * c_draw_id_stride);
    for (std::size_t i = 0; i < entry_count; ++i) {
        const int32_t draw_id = static_cast<int32_t>(i);
        std::memcpy(contents + (i * c_draw_id_stride), &draw_id, sizeof(draw_id));
    }
    m_residency.add(table);
    m_draw_id_tables.push_back(table);
    m_draw_id_capacity = entry_count;
    return true;
}

auto Submission_pools::get_draw_id_table(const std::size_t draw_count) -> uint64_t
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (draw_count > m_draw_id_capacity) {
        const std::size_t entry_count = std::bit_ceil(draw_count);
        log_context->info("Metal 4: draw id table grown to {} entries", entry_count);
        if (!create_draw_id_table(entry_count)) {
            ERHE_FATAL("Metal 4: could not grow the draw id table to %zu entries", entry_count);
        }
    }
    return m_draw_id_tables.back()->gpuAddress();
}

} // namespace erhe::graphics
