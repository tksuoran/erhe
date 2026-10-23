#include "erhe_graphics/metal4/metal4_submission.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <bit>
#include <cstring>

namespace erhe::graphics {

namespace {

constexpr MTL::Stages c_all_queue_stages =
    MTL::StageVertex   | MTL::StageFragment | MTL::StageTile | MTL::StageObject | MTL::StageMesh |
    MTL::StageDispatch | MTL::StageBlit     | MTL::StageAccelerationStructure;

constexpr MTL::Stages c_render_stages =
    MTL::StageVertex | MTL::StageFragment | MTL::StageTile | MTL::StageObject | MTL::StageMesh;

constexpr MTL::Stages c_compute_stages =
    MTL::StageDispatch | MTL::StageBlit | MTL::StageAccelerationStructure;

constexpr std::size_t c_initial_draw_id_count = 4096;

} // anonymous namespace

void encode_queue_barrier(MTL4::RenderCommandEncoder* const encoder)
{
    ERHE_VERIFY(encoder != nullptr);
    encoder->barrierAfterQueueStages(c_all_queue_stages, c_render_stages, MTL4::VisibilityOptionDevice);
}

void encode_queue_barrier(MTL4::ComputeCommandEncoder* const encoder)
{
    ERHE_VERIFY(encoder != nullptr);
    encoder->barrierAfterQueueStages(c_all_queue_stages, c_compute_stages, MTL4::VisibilityOptionDevice);
}

void encode_intra_encoder_barrier(MTL4::ComputeCommandEncoder* const encoder)
{
    ERHE_VERIFY(encoder != nullptr);
    encoder->barrierAfterEncoderStages(c_compute_stages, c_compute_stages, MTL4::VisibilityOptionDevice);
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
    recording->presents         = false;
    recording->present_frame_id = 0;
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
