// Draw_list_scene: the GPU culling classification (doc/plans/culling.md
// phase 5, doc/erhe/draw_list_renderer.md Q6 "GPU classification").

#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/gpu_cull_classifier.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>

namespace erhe::scene_renderer {

// The GPU mask readback of one frame and the CPU masks of the same frame
// (Draw_list_scene::request_cull_classification_check()). Shared with the
// completion handlers that copy the readbacks, so a scene destroyed before
// they ran leaves them a valid target. Every field is guarded by mutex.
// Each request starts a new generation; a completion handler of an older
// generation releases its readback and changes nothing.
class Gpu_cull_check
{
public:
    enum class Stage : uint8_t
    {
        idle,
        requested, // main thread asked; the next GPU classified frame records it
        recording, // that frame's dispatch copies its mask region
        complete   // the frame retired and the CPU half was captured
    };

    std::mutex                         mutex;
    Stage                              stage         {Stage::idle};
    uint64_t                           generation    {0};
    uint64_t                           frame_index   {0};
    // The frame's one dispatch (none for a frame without entries) and its
    // mask region, [group * entry_count + entry] (cull_classify.comp).
    uint32_t                           dispatch_count{0};
    std::vector<std::byte>             gpu_masks;
    std::size_t                        pending_reads {0};
    bool                               cpu_captured  {false};
    // The CPU half, captured at the recorded frame's classification.
    uint32_t                           entry_count{0};
    uint32_t                           view_count {0};
    std::vector<uint32_t>              list_entry_offsets;
    std::vector<uint32_t>              list_entry_counts;
    std::vector<Draw_purpose>          list_purposes;
    std::vector<std::vector<uint64_t>> cpu_masks;

    void complete_if_ready()
    {
        if (cpu_captured && (pending_reads == 0)) {
            stage = Stage::complete;
        }
    }
};

auto Draw_list_scene::make_gpu_cull_check() -> std::shared_ptr<Gpu_cull_check>
{
    return std::make_shared<Gpu_cull_check>();
}

namespace {

// Host flush granularity: a multiple of every nonCoherentAtomSize.
constexpr std::size_t c_gpu_bounds_flush_alignment = 256;

[[nodiscard]] auto align_down(const std::size_t value, const std::size_t alignment) -> std::size_t
{
    return (value / alignment) * alignment;
}

[[nodiscard]] auto align_up(const std::size_t value, const std::size_t alignment) -> std::size_t
{
    return ((value + alignment - 1) / alignment) * alignment;
}

} // anonymous namespace

void Draw_list_scene::publish_gpu_bounds(Draw_list_slot& slot)
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(m_gpu_cull_classifier != nullptr);
    erhe::graphics::Device& graphics_device = m_gpu_cull_classifier->get_device();

    if (!m_gpu_cull_timer) {
        m_gpu_cull_timer = std::make_unique<erhe::graphics::Gpu_timer>(graphics_device, "Cull classification");
    }

    const std::size_t list_count = slot.lists.size();
    const std::size_t byte_count = std::max(static_cast<std::size_t>(slot.entry_count), std::size_t{1}) * Gpu_cull_classifier::bounds_bytes_per_entry;

    // Growing replaces the slot's buffer and rewrites every list. The old
    // buffer's last frame retired (the slot's frame is being prepared).
    if (!slot.gpu_bounds || (slot.gpu_bounds_capacity < byte_count)) {
        const std::size_t capacity = align_up(byte_count + (byte_count / 2), c_gpu_bounds_flush_alignment);
        slot.gpu_bounds = std::make_unique<erhe::graphics::Buffer>(
            graphics_device,
            erhe::graphics::Buffer_create_info{
                .capacity_byte_count                    = capacity,
                .memory_allocation_create_flag_bit_mask = erhe::graphics::Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = erhe::graphics::Buffer_usage::storage,
                .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::host_coherent | erhe::graphics::Memory_property_flag_bit_mask::host_persistent,
                .debug_label                            = erhe::utility::Debug_label{"Draw_list_scene GPU cull bounds"}
            }
        );
        slot.gpu_bounds_capacity = capacity;
        slot.gpu_bounds_serials.clear();
    }
    // Lists only grow in count (rebuild_all() gives every list a new
    // serial); a new list starts with nothing written (serial 0).
    slot.gpu_bounds_serials.resize(list_count, uint64_t{0});
    slot.gpu_bounds_offsets.resize(list_count, uint32_t{0});

    // The lists whose slot copy changed since this slot's buffer was written
    // (the per-list serial copy_to_slot() compares), or whose range moved
    // because an earlier list changed size.
    std::size_t          dirty_begin = std::numeric_limits<std::size_t>::max();
    std::size_t          dirty_end   = 0;
    std::span<std::byte> mapped{};
    for (std::size_t list_index = 0; list_index < list_count; ++list_index) {
        const Draw_list& draw_list    = slot.lists[list_index];
        const uint32_t   entry_offset = slot.entry_offsets[list_index];
        if (
            (slot.gpu_bounds_serials[list_index] == draw_list.serial) &&
            (slot.gpu_bounds_offsets[list_index] == entry_offset)
        ) {
            continue;
        }
        slot.gpu_bounds_serials[list_index] = draw_list.serial;
        slot.gpu_bounds_offsets[list_index] = entry_offset;
        const std::size_t list_entry_count = draw_list.entries.size();
        if (list_entry_count == 0) {
            continue;
        }
        if (mapped.empty()) {
            mapped = slot.gpu_bounds->map_bytes(0, slot.gpu_bounds_capacity);
        }
        // Per entry: center.xyz, 0 and extent.xyz, purpose
        // (cull_classify.comp); the floats the CPU lanes read. A skinned
        // list's entries are written without bounds (negative extent), the
        // GPU form of the CPU rule that skinned lists keep every view of
        // their purpose.
        const Draw_list_bounds& bounds = draw_list.bounds;
        ERHE_VERIFY(bounds.extent_x.size() == list_entry_count);
        const bool        skinned      = (draw_list.key.mobility == Draw_mobility::skinned);
        const float       purpose      = static_cast<float>(static_cast<uint32_t>(draw_list.key.purpose));
        const std::size_t range_begin  = static_cast<std::size_t>(entry_offset) * Gpu_cull_classifier::bounds_bytes_per_entry;
        std::size_t       write_offset = range_begin;
        for (std::size_t i = 0; i < list_entry_count; ++i) {
            const std::array<float, 8> record{
                bounds.center_x[i], bounds.center_y[i], bounds.center_z[i], 0.0f,
                skinned ? -1.0f : bounds.extent_x[i], bounds.extent_y[i], bounds.extent_z[i], purpose
            };
            std::memcpy(mapped.data() + write_offset, record.data(), sizeof(record));
            write_offset += sizeof(record);
        }
        dirty_begin = std::min(dirty_begin, range_begin);
        dirty_end   = std::max(dirty_end, write_offset);
        ++m_gpu_bounds_write_count;
    }
    if (!mapped.empty()) {
        if (dirty_begin < dirty_end) {
            const std::size_t flush_begin = align_down(dirty_begin, c_gpu_bounds_flush_alignment);
            const std::size_t flush_end   = std::min(align_up(dirty_end, c_gpu_bounds_flush_alignment), slot.gpu_bounds_capacity);
            slot.gpu_bounds->flush_bytes(flush_begin, flush_end - flush_begin);
        }
        slot.gpu_bounds->unmap();
    }
}

void Draw_list_scene::dispatch_gpu_cull_views(const uint64_t frame_index, erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(m_gpu_cull_classifier != nullptr);
    ERHE_VERIFY(m_cull_frame_classified);

    const Draw_list_slot& slot        = get_published_slot(frame_index);
    const std::size_t     entry_count = slot.entry_count;
    const std::size_t     view_count  = m_cull_views.size();

    // A requested check records this frame (the first one classified on the
    // GPU after the request); its generation, else none.
    std::optional<uint64_t> check_generation{};
    {
        const std::lock_guard<std::mutex> lock{m_gpu_cull_check->mutex};
        if (m_gpu_cull_check->stage == Gpu_cull_check::Stage::requested) {
            m_gpu_cull_check->stage       = Gpu_cull_check::Stage::recording;
            m_gpu_cull_check->frame_index = frame_index;
        }
        if (
            (m_gpu_cull_check->stage == Gpu_cull_check::Stage::recording) &&
            (m_gpu_cull_check->frame_index == frame_index)
        ) {
            check_generation = m_gpu_cull_check->generation;
        }
    }

    // No entries, no masks: nothing to dispatch.
    if ((entry_count > 0) && (view_count > 0)) {
        Gpu_cull_classifier& classifier   = *m_gpu_cull_classifier;
        Device&              device       = classifier.get_device();
        const std::size_t    group_count  = (view_count + 63) / 64;
        const std::size_t    mask_bytes   = group_count * entry_count * Gpu_cull_classifier::mask_bytes_per_entry_group;
        const std::size_t    view_bytes   = classifier.get_view_data_offset() + (view_count * Gpu_cull_classifier::vec4s_per_view * sizeof(glm::vec4));
        const std::size_t    bounds_bytes = entry_count * Gpu_cull_classifier::bounds_bytes_per_entry;
        ERHE_VERIFY(slot.gpu_bounds && (slot.gpu_bounds_capacity >= bounds_bytes));

        // The frame's views, transcribed from the CPU's Cull_view_tests (the
        // single source) in the cull_classify.comp layout.
        Ring_buffer_range view_range = classifier.get_view_buffer().acquire(Ring_buffer_usage::CPU_write, view_bytes);
        {
            const std::span<std::byte> gpu_data = view_range.get_span();
            const glm::uvec4 header{static_cast<uint32_t>(view_count), static_cast<uint32_t>(entry_count), 0u, 0u};
            std::memcpy(gpu_data.data() + classifier.get_view_header_offset(), &header, sizeof(header));
            std::size_t write_offset = classifier.get_view_data_offset();
            for (std::size_t view_index = 0; view_index < view_count; ++view_index) {
                const Cull_view_tests& view = m_cull_views[view_index];
                std::array<glm::vec4, Gpu_cull_classifier::vec4s_per_view> record{};
                for (uint32_t plane = 0; plane < view.plane_count; ++plane) {
                    record[plane] = view.planes[plane];
                }
                record[6] = glm::vec4{view.sphere.center, view.sphere.radius};
                record[7] = glm::vec4{
                    static_cast<float>(static_cast<uint32_t>(view.purpose)),
                    static_cast<float>(static_cast<uint32_t>(view.kind)),
                    static_cast<float>(view.plane_count),
                    static_cast<float>(view.cube_face)
                };
                std::memcpy(gpu_data.data() + write_offset, record.data(), sizeof(record));
                write_offset += sizeof(record);
            }
            view_range.bytes_written(view_bytes);
            view_range.close();
        }

        // The frame's mask region: released to its ring right after the
        // dispatch, which keeps the memory until the frame retired.
        Ring_buffer_range mask_range = classifier.get_mask_buffer().acquire(Ring_buffer_usage::GPU_access, mask_bytes);
        mask_range.bytes_gpu_used(mask_bytes);
        mask_range.close();
        const Buffer* const mask_buffer      = mask_range.get_buffer()->get_buffer();
        const std::size_t   mask_byte_offset = mask_range.get_byte_start_offset_in_buffer();

        {
            const Scoped_gpu_timer timer_scope{*m_gpu_cull_timer, command_buffer};
            Compute_command_encoder encoder = device.make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(classifier.get_bind_group_layout());
            encoder.set_compute_pipeline(classifier.get_pipeline());
            encoder.set_buffer(Buffer_target::storage, slot.gpu_bounds.get(), 0, bounds_bytes, Gpu_cull_classifier::bounds_binding_point);
            classifier.get_view_buffer().bind(encoder, view_range);
            encoder.set_buffer(Buffer_target::storage, mask_buffer, mask_byte_offset, mask_bytes, Gpu_cull_classifier::masks_binding_point);
            const std::size_t group_size = Gpu_cull_classifier::workgroup_size;
            encoder.dispatch_compute(static_cast<std::uintptr_t>((entry_count + group_size - 1) / group_size), 1, 1);
        }
        // No pass reads the masks yet: the GPU command generation of phase 6
        // adds the barrier from these shader writes to its compute reads.

        if (check_generation.has_value()) {
            // Shader writes -> the copy into the host-read buffer, read once
            // the frame retired.
            command_buffer.memory_barrier(Memory_barrier_mask::pixel_buffer_barrier_bit);
            std::shared_ptr<Ring_buffer_range> readback = std::make_shared<Ring_buffer_range>(
                classifier.get_readback_buffer().acquire(Ring_buffer_usage::CPU_read, mask_bytes)
            );
            {
                Blit_command_encoder blit = device.make_blit_command_encoder(command_buffer);
                blit.copy_from_buffer(
                    mask_buffer, mask_byte_offset,
                    readback->get_buffer()->get_buffer(), readback->get_byte_start_offset_in_buffer(),
                    mask_bytes
                );
            }
            {
                const std::lock_guard<std::mutex> lock{m_gpu_cull_check->mutex};
                m_gpu_cull_check->dispatch_count = 1;
                ++m_gpu_cull_check->pending_reads;
            }
            device.add_completion_handler(
                [check = m_gpu_cull_check, readback, generation = check_generation.value()]() {
                    const std::span<std::byte> gpu_data = readback->get_span();
                    {
                        const std::lock_guard<std::mutex> lock{check->mutex};
                        if ((check->generation == generation) && (check->stage == Gpu_cull_check::Stage::recording)) {
                            check->gpu_masks.assign(gpu_data.begin(), gpu_data.end());
                            --check->pending_reads;
                            check->complete_if_ready();
                        }
                    }
                    readback->bytes_gpu_used(gpu_data.size_bytes());
                    readback->close();
                    readback->release();
                }
            );
        }

        view_range.release();
        mask_range.release();
    }

    if (!check_generation.has_value()) {
        return;
    }
    // The CPU half of the check frame: the frame's masks are complete (one
    // classification per frame). A diagnostic path: it may allocate.
    const std::lock_guard<std::mutex> lock{m_gpu_cull_check->mutex};
    Gpu_cull_check& check = *m_gpu_cull_check;
    if ((check.generation != check_generation.value()) || (check.stage != Gpu_cull_check::Stage::recording)) {
        return;
    }
    check.entry_count             = slot.entry_count;
    check.view_count              = static_cast<uint32_t>(view_count);
    const std::size_t list_count  = slot.lists.size();
    const std::size_t group_count = (view_count + 63) / 64;
    check.list_entry_offsets.assign(slot.entry_offsets.begin(), slot.entry_offsets.end());
    check.list_entry_counts.resize(list_count);
    check.list_purposes.resize(list_count);
    check.cpu_masks.resize(list_count);
    for (std::size_t list_index = 0; list_index < list_count; ++list_index) {
        const Draw_list&  draw_list         = slot.lists[list_index];
        const std::size_t list_entry_count  = draw_list.entries.size();
        check.list_entry_counts[list_index] = static_cast<uint32_t>(list_entry_count);
        check.list_purposes    [list_index] = draw_list.key.purpose;
        std::vector<uint64_t>& masks = check.cpu_masks[list_index];
        masks.assign(group_count * list_entry_count, uint64_t{0});
        if (list_index < m_cull_masks.size()) {
            const std::vector<uint64_t>& source = m_cull_masks[list_index];
            std::copy_n(source.begin(), std::min(source.size(), masks.size()), masks.begin());
        }
    }
    check.cpu_captured = true;
    check.complete_if_ready();
}

void Draw_list_scene::request_cull_classification_check()
{
    assert_main_thread();
    // A new request replaces any earlier one, whatever its stage: its
    // pending readbacks find a newer generation and are dropped.
    const std::lock_guard<std::mutex> lock{m_gpu_cull_check->mutex};
    Gpu_cull_check& check = *m_gpu_cull_check;
    ++check.generation;
    check.stage          = Gpu_cull_check::Stage::requested;
    check.dispatch_count = 0;
    check.gpu_masks.clear();
    check.pending_reads  = 0;
    check.cpu_captured  = false;
    check.cpu_masks.clear();
}

auto Draw_list_scene::poll_cull_classification_check(Cull_classification_check& out) -> Cull_classification_check_state
{
    assert_main_thread();
    const std::lock_guard<std::mutex> lock{m_gpu_cull_check->mutex};
    Gpu_cull_check& check = *m_gpu_cull_check;
    switch (check.stage) {
        case Gpu_cull_check::Stage::idle:      return Cull_classification_check_state::idle;
        case Gpu_cull_check::Stage::requested: return Cull_classification_check_state::pending;
        case Gpu_cull_check::Stage::recording: return Cull_classification_check_state::pending;
        case Gpu_cull_check::Stage::complete:  break;
        default:                               return Cull_classification_check_state::idle;
    }

    constexpr std::size_t max_reported_mismatches = 8;
    out = Cull_classification_check{};
    out.frame_index    = check.frame_index;
    out.entry_count    = check.entry_count;
    out.view_count     = check.view_count;
    out.dispatch_count = check.dispatch_count;
    out.gpu_time_ns    = m_gpu_cull_timer ? m_gpu_cull_timer->last_result() : 0;

    for (std::size_t list_index = 0, list_end = check.list_entry_counts.size(); list_index < list_end; ++list_index) {
        const uint32_t list_entry_count = check.list_entry_counts[list_index];
        if (list_entry_count == 0) {
            continue;
        }
        Cull_classification_list_check list_check{
            .list_index  = static_cast<uint32_t>(list_index),
            .purpose     = check.list_purposes[list_index],
            .entry_count = list_entry_count
        };
        const std::vector<uint64_t>& cpu_masks = check.cpu_masks[list_index];
        // A list with entries means the frame had entries: one dispatch.
        ERHE_VERIFY((check.dispatch_count == 1) || (check.view_count == 0));
        for (uint32_t view = 0; view < check.view_count; ++view) {
            const std::size_t group = view / 64;
            const uint32_t    bit   = view % 64;
            for (uint32_t entry = 0; entry < list_entry_count; ++entry) {
                const uint32_t cpu_bit = static_cast<uint32_t>((cpu_masks[(group * list_entry_count) + entry] >> bit) & uint64_t{1});
                const std::size_t element = (group * check.entry_count) + check.list_entry_offsets[list_index] + entry;
                ERHE_VERIFY(((element + 1) * Gpu_cull_classifier::mask_bytes_per_entry_group) <= check.gpu_masks.size());
                std::array<uint32_t, 2> gpu_words{0u, 0u};
                std::memcpy(gpu_words.data(), check.gpu_masks.data() + (element * Gpu_cull_classifier::mask_bytes_per_entry_group), sizeof(gpu_words));
                const uint32_t gpu_bit = (bit < 32) ? ((gpu_words[0] >> bit) & 1u) : ((gpu_words[1] >> (bit - 32)) & 1u);
                if (cpu_bit != gpu_bit) {
                    ++list_check.mismatch_count;
                    if (list_check.mismatches.size() < max_reported_mismatches) {
                        list_check.mismatches.push_back(Cull_classification_mismatch{
                            .entry_index = entry,
                            .view        = view,
                            .cpu_bit     = cpu_bit,
                            .gpu_bit     = gpu_bit
                        });
                    }
                }
            }
        }
        out.mismatch_count += list_check.mismatch_count;
        out.lists.push_back(std::move(list_check));
    }

    check.stage = Gpu_cull_check::Stage::idle;
    check.gpu_masks.clear();
    check.cpu_masks.clear();
    return Cull_classification_check_state::complete;
}

} // namespace erhe::scene_renderer
