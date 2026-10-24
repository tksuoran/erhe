// Draw_list_scene: the GPU culling classification (doc/plans/culling.md
// phase 5, doc/erhe/draw_list_renderer.md Q6 "GPU classification").

#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/cull_frame_registry.hpp"
#include "erhe_scene_renderer/gpu_cull_classifier.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/indirect_count_draws.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <bit>
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
    // The frame's occlusion culled region (empty: the frame consumed no
    // occlusion words): the mask bits the occlusion consumption removed.
    std::vector<std::byte>             gpu_culled;
    std::size_t                        pending_reads {0};
    bool                               cpu_captured  {false};
    // The CPU half, captured at the recorded frame's classification.
    uint32_t                           entry_count{0};
    uint32_t                           view_count {0};
    std::vector<uint32_t>              list_entry_offsets;
    std::vector<uint32_t>              list_entry_counts;
    std::vector<Draw_purpose>          list_purposes;
    std::vector<std::vector<uint64_t>> cpu_masks;

    // The frame's GPU command generation (doc/plans/culling.md phase 6):
    // the jobs with what the CPU command writer produces for each from the
    // frame's CPU masks (captured at the generation), and the GPU-written
    // counts and commands (read back).
    class Command_job
    {
    public:
        uint32_t view          {0};
        uint32_t list_index    {0};
        uint32_t chunk_begin   {0}; // in the list
        uint32_t entry_count   {0};
        uint32_t command_offset{0};
        bool     color         {false}; // a color selection's job
        // The job's commands the CPU writer produces from the CPU masks
        // (frustum and filter), before the occlusion term: candidates
        // [candidate_begin, candidate_begin + candidate_count).
        uint32_t candidate_begin{0};
        uint32_t candidate_count{0};
    };
    // One CPU command candidate: its entry (all lists back to back) and the
    // command; the poll drops the candidates whose view bit the GPU's
    // occlusion consumption removed (gpu_culled).
    class Command_candidate
    {
    public:
        uint32_t                                                 entry{0};
        erhe::graphics::Draw_indexed_primitives_indirect_command command{};
    };
    std::vector<Command_job>                                              command_jobs;
    std::vector<Command_candidate>                                        command_candidates;
    std::vector<std::byte>                                                gpu_counts;
    std::vector<std::byte>                                                gpu_commands;

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

// The occlusion check of one frame (Draw_list_scene::request_occlusion_check()):
// requested and polled on the main thread, captured at the recorded frame's
// classification and decoded at the classification of the frame that reuses
// its occlusion slot, both on the render thread. Every field is guarded by
// mutex. A diagnostic: it may allocate.
class Gpu_occlusion_check
{
public:
    enum class Stage : uint8_t
    {
        idle,
        requested, // main thread asked; the next frame with occlusion views records it
        recording, // that frame was captured; its words are decoded when it retired
        complete   // decoded: result is ready
    };

    class View
    {
    public:
        Cull_view             cull_view         {Cull_view::none};
        Gpu_command_key       key               {};
        uint32_t              bounded_in_frustum{0};
        std::vector<uint32_t> near_plane_entries; // all lists back to back
    };

    std::mutex            mutex;
    Stage                 stage      {Stage::idle};
    uint64_t              frame_index{0};
    // The CPU half, captured at the recorded frame's classification.
    uint32_t              entry_count{0};
    uint32_t              view_count {0};
    std::vector<uint32_t> list_entry_offsets;
    std::vector<uint32_t> entry_objects; // per entry (all lists): Draw_list_entry::object_index
    std::vector<View>     views;
    // The decoded result; the first occluded entries' object indices per
    // view, named at the poll (main thread).
    Occlusion_check                    result;
    std::vector<std::vector<uint32_t>> first_occluded_objects;
};

auto Draw_list_scene::make_gpu_occlusion_check() -> std::shared_ptr<Gpu_occlusion_check>
{
    return std::make_shared<Gpu_occlusion_check>();
}

namespace {

// Host flush granularity: a multiple of every nonCoherentAtomSize.
constexpr std::size_t c_gpu_bounds_flush_alignment = 256;

// The occlusion buffer's smallest region (a dispatch without occlusion
// views binds the two regions of an otherwise unused buffer) and the
// alignment of the result region (a storage binding offset; a multiple of
// every device's minStorageBufferOffsetAlignment and nonCoherentAtomSize).
constexpr std::size_t c_occlusion_min_region_bytes = 256;
constexpr std::size_t c_occlusion_region_alignment = 256;

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

    // Growing replaces the slot's buffers and rewrites every list. The old
    // buffers' last frame retired (the slot's frame is being prepared). The
    // command templates (GPU command generation, doc/plans/culling.md phase
    // 6) have the bounds' entry size, so both buffers share the capacity
    // and the entry offsets.
    static_assert(Gpu_cull_classifier::template_bytes_per_entry == Gpu_cull_classifier::bounds_bytes_per_entry);
    if (!slot.gpu_bounds || (slot.gpu_bounds_capacity < byte_count)) {
        const std::size_t capacity = align_up(byte_count + (byte_count / 2), c_gpu_bounds_flush_alignment);
        const auto make_buffer = [&](const char* label) -> std::unique_ptr<erhe::graphics::Buffer> {
            return std::make_unique<erhe::graphics::Buffer>(
                graphics_device,
                erhe::graphics::Buffer_create_info{
                    .capacity_byte_count                    = capacity,
                    .memory_allocation_create_flag_bit_mask = erhe::graphics::Memory_allocation_create_flag_bit_mask::mapped,
                    .usage                                  = erhe::graphics::Buffer_usage::storage,
                    .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::host_write,
                    .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::host_coherent | erhe::graphics::Memory_property_flag_bit_mask::host_persistent,
                    .debug_label                            = erhe::utility::Debug_label{label}
                }
            );
        };
        slot.gpu_bounds          = make_buffer("Draw_list_scene GPU cull bounds");
        slot.gpu_templates       = make_buffer("Draw_list_scene GPU command templates");
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
    std::span<std::byte> mapped_templates{};
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
            mapped           = slot.gpu_bounds   ->map_bytes(0, slot.gpu_bounds_capacity);
            mapped_templates = slot.gpu_templates->map_bytes(0, slot.gpu_bounds_capacity);
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
            // The command template and the flag bits the shadow passes'
            // filter reads (cull_compact.comp): the fields the CPU command
            // writer reads from the entry.
            const Draw_list_entry& entry = draw_list.entries[i];
            const std::array<uint32_t, 2> flag_words = Gpu_cull_classifier::split_uint64(entry.flag_bits);
            const std::array<uint32_t, 8> command_template{
                entry.index_count, entry.first_index, entry.base_vertex, 0u,
                flag_words[0], flag_words[1], 0u, 0u
            };
            std::memcpy(mapped.data()           + write_offset, record.data(),           sizeof(record));
            std::memcpy(mapped_templates.data() + write_offset, command_template.data(), sizeof(command_template));
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
            slot.gpu_bounds   ->flush_bytes(flush_begin, flush_end - flush_begin);
            slot.gpu_templates->flush_bytes(flush_begin, flush_end - flush_begin);
        }
        slot.gpu_bounds   ->unmap();
        slot.gpu_templates->unmap();
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
        // The raster occlusion test's start words (doc/plans/
        // occlusion_culling.md): written by this dispatch when the frame has
        // occlusion views, into the frame slot's occlusion buffer (bound
        // every dispatch; the shader writes it only when the header asks).
        const bool occlusion = !m_occlusion_views.empty();
        prepare_occlusion_slot(frame_index, entry_count, group_count, occlusion ? Occlusion_culling::on : Occlusion_culling::off);
        Occlusion_slot& occlusion_slot = m_occlusion_slots[frame_index % m_occlusion_slots.size()];
        // The previous frame's occlusion words, consumed by this frame's
        // classification (one frame of lag): only those of the frame right
        // before this one, describing the same entries at the same
        // positions (no entry added, removed or moved since); otherwise no
        // view is culled by occlusion this frame.
        const Occlusion_slot* previous_slot = nullptr;
        if (occlusion && m_has_occlusion_frame && ((m_occlusion_frame_index + 1) == frame_index)) {
            const Occlusion_slot& candidate = m_occlusion_slots[m_occlusion_frame_index % m_occlusion_slots.size()];
            if (
                (&candidate != &occlusion_slot) &&
                candidate.pending &&
                (candidate.frame_index == m_occlusion_frame_index) &&
                (candidate.entry_count == entry_count) &&
                (candidate.entry_layout_serial == slot.entry_layout_serial)
            ) {
                previous_slot = &candidate;
            }
        }

        Ring_buffer_range view_range = classifier.get_view_buffer().acquire(Ring_buffer_usage::CPU_write, view_bytes);
        {
            const std::span<std::byte> gpu_data = view_range.get_span();
            const glm::uvec4 header{static_cast<uint32_t>(view_count), static_cast<uint32_t>(entry_count), occlusion ? 1u : 0u, (previous_slot != nullptr) ? 1u : 0u};
            std::memcpy(gpu_data.data() + classifier.get_view_header_offset(), &header, sizeof(header));
            const glm::uvec4 occlusion_header{
                static_cast<uint32_t>(occlusion_slot.result_offset / sizeof(uint32_t)),
                static_cast<uint32_t>(occlusion_slot.culled_offset / sizeof(uint32_t)),
                (previous_slot != nullptr) ? static_cast<uint32_t>(previous_slot->result_offset / sizeof(uint32_t)) : 0u,
                0u
            };
            std::memcpy(gpu_data.data() + classifier.get_view_occlusion_header_offset(), &occlusion_header, sizeof(occlusion_header));
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
                // The occlusion test of an occlusion view: its near plane,
                // the flag and, when the previous frame's words hold the
                // same view (matched by key: view indices differ between
                // frames), that view's group and bit (cull_classify.comp).
                for (const Occlusion_view_record& occlusion_view : m_occlusion_views) {
                    if (static_cast<std::size_t>(occlusion_view.cull_view) != view_index) {
                        continue;
                    }
                    record[8] = occlusion_view.near_plane;
                    record[9] = glm::vec4{1.0f, 0.0f, 0.0f, 0.0f};
                    if (previous_slot != nullptr) {
                        for (const Occlusion_view_record& previous_view : previous_slot->views) {
                            if (previous_view.key == occlusion_view.key) {
                                const uint32_t previous_index = static_cast<uint32_t>(previous_view.cull_view);
                                record[9] = glm::vec4{1.0f, 1.0f, static_cast<float>(previous_index / 64), static_cast<float>(previous_index % 64)};
                                break;
                            }
                        }
                    }
                    if (record[9].y > 0.5f) {
                        break;
                    }
                }
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

        if (previous_slot != nullptr) {
            // The previous frame's classification and occlusion pass
            // fragments wrote the words this dispatch reads; that frame's
            // command buffer was submitted before this one.
            command_buffer.buffer_barrier(
                *previous_slot->buffer,
                Buffer_usage_flag_bit_mask::storage_write | Buffer_usage_flag_bit_mask::compute_shader | Buffer_usage_flag_bit_mask::fragment_shader,
                Buffer_usage_flag_bit_mask::storage_read | Buffer_usage_flag_bit_mask::compute_shader,
                Barrier_scope::earlier_submissions
            );
        }
        {
            const Scoped_gpu_timer timer_scope{*m_gpu_cull_timer, command_buffer};
            Compute_command_encoder encoder = device.make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(classifier.get_bind_group_layout());
            encoder.set_compute_pipeline(classifier.get_pipeline());
            encoder.set_buffer(Buffer_target::storage, slot.gpu_bounds.get(), 0, bounds_bytes, Gpu_cull_classifier::bounds_binding_point);
            classifier.get_view_buffer().bind(encoder, view_range);
            encoder.set_buffer(Buffer_target::storage, mask_buffer, mask_byte_offset, mask_bytes, Gpu_cull_classifier::masks_binding_point);
            // The slot's three regions; the previous frame's test and result
            // regions, or a buffer the shader does not read.
            encoder.set_buffer(Buffer_target::storage, occlusion_slot.buffer.get(), 0, occlusion_slot.culled_offset + occlusion_slot.bind_bytes, Gpu_cull_classifier::occlusion_words_binding_point);
            if (previous_slot != nullptr) {
                encoder.set_buffer(Buffer_target::storage, previous_slot->buffer.get(), 0, previous_slot->result_offset + previous_slot->bind_bytes, Gpu_cull_classifier::occlusion_previous_words_binding_point);
            } else {
                encoder.set_buffer(Buffer_target::storage, m_occlusion_no_previous_buffer.get(), 0, c_occlusion_min_region_bytes, Gpu_cull_classifier::occlusion_previous_words_binding_point);
            }
            const std::size_t group_size = Gpu_cull_classifier::workgroup_size;
            encoder.dispatch_compute(static_cast<std::uintptr_t>((entry_count + group_size - 1) / group_size), 1, 1);
        }
        if (occlusion) {
            // The start words -> the occlusion passes' vertex reads (test
            // region) and fragment atomics (result region), in this frame's
            // view render passes.
            command_buffer.buffer_barrier(
                *occlusion_slot.buffer,
                Buffer_usage_flag_bit_mask::storage_write | Buffer_usage_flag_bit_mask::compute_shader,
                Buffer_usage_flag_bit_mask::storage_read | Buffer_usage_flag_bit_mask::storage_write | Buffer_usage_flag_bit_mask::vertex_shader | Buffer_usage_flag_bit_mask::fragment_shader
            );
            occlusion_slot.frame_index = frame_index;
            occlusion_slot.entry_count = static_cast<uint32_t>(entry_count);
            occlusion_slot.group_count = static_cast<uint32_t>((view_count + 63) / 64);
            occlusion_slot.views.assign(m_occlusion_views.begin(), m_occlusion_views.end());
            occlusion_slot.entry_layout_serial = slot.entry_layout_serial;
            occlusion_slot.consumed_previous   = (previous_slot != nullptr);
            occlusion_slot.pending     = true;
            m_occlusion_frame_index    = frame_index;
            m_occlusion_frame_active   = true;
            m_has_occlusion_frame      = true;
            // The host reads the words once the frame retired: the frame's
            // end orders its writes for it.
            m_cull_frame_registry.add_frame_end_scene(*this, frame_index);
            capture_occlusion_check(frame_index);
        }
        // The passes' commands from the masks (phase 6), when the frame's
        // shadow renderer gave its selection or color passes gave theirs.
        if ((m_shadow_selection_set && (m_shadow_selection_frame_index == frame_index)) || !m_color_selections.empty()) {
            generate_gpu_commands(frame_index, command_buffer, *mask_buffer, mask_byte_offset, mask_bytes, check_generation);
        }

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
            // The bits the occlusion consumption removed from the masks
            // (the culled region, mask layout): the check compares the GPU
            // masks with them added back.
            if (occlusion && (previous_slot != nullptr)) {
                std::shared_ptr<Ring_buffer_range> culled_readback = std::make_shared<Ring_buffer_range>(
                    classifier.get_readback_buffer().acquire(Ring_buffer_usage::CPU_read, mask_bytes)
                );
                {
                    Blit_command_encoder blit = device.make_blit_command_encoder(command_buffer);
                    blit.copy_from_buffer(
                        occlusion_slot.buffer.get(), occlusion_slot.culled_offset,
                        culled_readback->get_buffer()->get_buffer(), culled_readback->get_byte_start_offset_in_buffer(),
                        mask_bytes
                    );
                }
                {
                    const std::lock_guard<std::mutex> lock{m_gpu_cull_check->mutex};
                    ++m_gpu_cull_check->pending_reads;
                }
                device.add_completion_handler(
                    [check = m_gpu_cull_check, culled_readback, generation = check_generation.value()]() {
                        const std::span<std::byte> gpu_data = culled_readback->get_span();
                        {
                            const std::lock_guard<std::mutex> lock{check->mutex};
                            if ((check->generation == generation) && (check->stage == Gpu_cull_check::Stage::recording)) {
                                check->gpu_culled.assign(gpu_data.begin(), gpu_data.end());
                                --check->pending_reads;
                                check->complete_if_ready();
                            }
                        }
                        culled_readback->bytes_gpu_used(gpu_data.size_bytes());
                        culled_readback->close();
                        culled_readback->release();
                    }
                );
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
    check.command_jobs.clear();
    check.command_candidates.clear();
    check.gpu_culled.clear();
    check.gpu_counts.clear();
    check.gpu_commands.clear();
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

    // The bit the frame's occlusion consumption removed for (view, entry of
    // all lists), from the culled region (0 when none was consumed). The
    // CPU masks know no occlusion: a removed bit counts as the GPU's bit,
    // and a removal of a bit the CPU did not set is a mismatch.
    const auto culled_bit = [&check](const uint32_t view, const std::size_t entry) -> uint32_t {
        if (check.gpu_culled.empty()) {
            return 0u;
        }
        const std::size_t element = ((view / 64) * check.entry_count) + entry;
        ERHE_VERIFY(((element + 1) * Gpu_cull_classifier::mask_bytes_per_entry_group) <= check.gpu_culled.size());
        std::array<uint32_t, 2> words{0u, 0u};
        std::memcpy(words.data(), check.gpu_culled.data() + (element * Gpu_cull_classifier::mask_bytes_per_entry_group), sizeof(words));
        const uint32_t bit = view % 64;
        return (bit < 32) ? ((words[0] >> bit) & 1u) : ((words[1] >> (bit - 32)) & 1u);
    };

    for (std::size_t list_index = 0, list_end = check.list_entry_counts.size(); list_index < list_end; ++list_index) {
        const uint32_t list_entry_count = check.list_entry_counts[list_index];
        if (list_entry_count == 0) {
            continue;
        }
        Cull_classification_list_check list_check{};
        list_check.list_index  = static_cast<uint32_t>(list_index);
        list_check.purpose     = check.list_purposes[list_index];
        list_check.entry_count = list_entry_count;
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
                const uint32_t culled  = culled_bit(view, check.list_entry_offsets[list_index] + entry);
                out.occlusion_culled_count += culled;
                if ((cpu_bit != (gpu_bit | culled)) || ((culled != 0u) && (gpu_bit != 0u))) {
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

    // The GPU command generation: per job, the GPU count and the commands
    // up to it against what the CPU writer produces.
    constexpr std::size_t command_stride = sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command);
    out.job_count           = static_cast<uint32_t>(check.command_jobs.size());
    out.color_job_count     = 0;
    out.command_count       = 0;
    out.command_gpu_time_ns = m_gpu_command_timer ? m_gpu_command_timer->last_result() : 0;
    for (std::size_t job_index = 0, job_end = check.command_jobs.size(); job_index < job_end; ++job_index) {
        const Gpu_cull_check::Command_job& job = check.command_jobs[job_index];
        ERHE_VERIFY(((job_index + 1) * Gpu_cull_classifier::count_bytes_per_job) <= check.gpu_counts.size());
        uint32_t gpu_count = 0;
        std::memcpy(&gpu_count, check.gpu_counts.data() + (job_index * Gpu_cull_classifier::count_bytes_per_job), sizeof(gpu_count));
        if (job.color) {
            ++out.color_job_count;
        }
        // The CPU writer's commands with the GPU's occlusion removals
        // applied, compared in order with the GPU's.
        uint32_t cpu_count          = 0;
        uint32_t command_mismatches = 0;
        for (uint32_t candidate = job.candidate_begin, end = job.candidate_begin + job.candidate_count; candidate < end; ++candidate) {
            const Gpu_cull_check::Command_candidate& command = check.command_candidates[candidate];
            if (culled_bit(job.view, command.entry) != 0u) {
                continue;
            }
            if (cpu_count < std::min(gpu_count, job.entry_count)) {
                const std::size_t position = static_cast<std::size_t>(job.command_offset) + cpu_count;
                ERHE_VERIFY(((position + 1) * command_stride) <= check.gpu_commands.size());
                if (std::memcmp(check.gpu_commands.data() + (position * command_stride), &command.command, command_stride) != 0) {
                    ++command_mismatches;
                }
            }
            ++cpu_count;
        }
        out.command_count += cpu_count;
        if ((gpu_count != cpu_count) || (command_mismatches > 0)) {
            ++out.command_mismatch_count;
            if (out.command_mismatches.size() < max_reported_mismatches) {
                out.command_mismatches.push_back(Cull_command_job_mismatch{
                    .job                    = static_cast<uint32_t>(job_index),
                    .view                   = job.view,
                    .list_index             = job.list_index,
                    .chunk_begin            = job.chunk_begin,
                    .cpu_count              = cpu_count,
                    .gpu_count              = gpu_count,
                    .command_mismatch_count = command_mismatches
                });
            }
        }
        const std::vector<Cull_command_view_check>::iterator view_it = std::find_if(
            out.command_views.begin(), out.command_views.end(),
            [&job](const Cull_command_view_check& view_check) -> bool { return view_check.view == job.view; }
        );
        Cull_command_view_check& view_check = (view_it != out.command_views.end())
            ? *view_it
            : out.command_views.emplace_back(Cull_command_view_check{.view = job.view});
        view_check.cpu_drawn += cpu_count;
        view_check.gpu_drawn += gpu_count;
    }

    check.stage = Gpu_cull_check::Stage::idle;
    check.gpu_masks.clear();
    check.cpu_masks.clear();
    check.command_jobs.clear();
    check.command_candidates.clear();
    check.gpu_culled.clear();
    check.gpu_counts.clear();
    check.gpu_commands.clear();
    return Cull_classification_check_state::complete;
}

void Draw_list_scene::generate_gpu_commands(
    const uint64_t                  frame_index,
    erhe::graphics::Command_buffer& command_buffer,
    const erhe::graphics::Buffer&   mask_buffer,
    const std::size_t               mask_byte_offset,
    const std::size_t               mask_byte_count,
    const std::optional<uint64_t>   check_generation
)
{
    using namespace erhe::graphics;

    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(m_gpu_cull_classifier != nullptr);
    ERHE_VERIFY(m_cull_frame_classified && (m_cull_frame_index == frame_index));
    const bool shadow_selection = m_shadow_selection_set && (m_shadow_selection_frame_index == frame_index);
    ERHE_VERIFY(shadow_selection || !m_color_selections.empty());

    const Draw_list_slot& slot       = get_published_slot(frame_index);
    const std::size_t     list_count = slot.lists.size();
    const std::size_t     view_count = m_cull_views.size();

    // The job table: per shadow view of the frame, per published shadow list
    // the shadow selection's layers select, then per color selection, per
    // published color list its blending classes (in the order draw_color()
    // draws them) and layers select, one job per chunk (the chunks
    // draw_list_chunks() cuts with the same chunk size), each with its own
    // dense command range, starting at a multiple of 32 (its pass bit words
    // start at command offset / 32). Persistent scratch, capacity kept.
    m_gpu_jobs.clear();
    m_gpu_job_infos.clear();
    m_gpu_job_filters.clear();
    m_gpu_indirect_count_jobs.clear();
    m_gpu_indirect_count_index_buffers.clear();
    m_gpu_shadow_view_jobs.assign(view_count, Gpu_job_range{});
    m_gpu_job_list_count = list_count;
    m_gpu_shadow_job_lookup.assign(view_count * list_count, c_no_gpu_job);
    m_gpu_color_job_lookup.assign(m_color_selections.size() * list_count, c_no_gpu_job);
    constexpr uint32_t command_alignment = Gpu_cull_classifier::job_command_alignment;
    uint32_t command_count   = 0;
    uint32_t max_job_entries = 0;
    // The jobs of one list for one view, filter and chunk size.
    const auto add_list_jobs = [&](
        const std::size_t          list_index,
        const std::size_t          view_index,
        const uint32_t             filter_index,
        const std::size_t          max_per_chunk,
        const Command_selection_id selection
    ) {
        const Draw_list&  draw_list        = slot.lists[list_index];
        const std::size_t list_entry_count = draw_list.entries.size();
        // The list's index buffer in the prepare's table (Mesh_memory pools
        // hold several buffers; a frame uses few, so a linear search).
        const erhe::graphics::Buffer* const index_buffer = m_mesh_memory.get_index_buffer(draw_list.key.buffer_set.index_buffer);
        ERHE_VERIFY(index_buffer != nullptr);
        uint32_t index_buffer_index = 0;
        while (
            (index_buffer_index < m_gpu_indirect_count_index_buffers.size()) &&
            (m_gpu_indirect_count_index_buffers[index_buffer_index].buffer != index_buffer)
        ) {
            ++index_buffer_index;
        }
        if (index_buffer_index == m_gpu_indirect_count_index_buffers.size()) {
            m_gpu_indirect_count_index_buffers.push_back(
                Indirect_count_index_buffer{
                    .buffer     = index_buffer,
                    .index_type = m_mesh_memory.get_index_format(draw_list.key.buffer_set.index_buffer)
                }
            );
        }
        // The packed view (Cull_view is 16 bits wide) and filter index.
        ERHE_VERIFY(view_index < 0x10000u);
        const uint32_t view_and_filter = static_cast<uint32_t>(view_index) | (filter_index << 16u);
        for (std::size_t begin = 0; begin < list_entry_count; begin += max_per_chunk) {
            const std::size_t end             = std::min(list_entry_count, begin + max_per_chunk);
            const uint32_t    job_entry_count = static_cast<uint32_t>(end - begin);
            m_gpu_jobs.push_back(Gpu_cull_job{
                .view_and_filter = view_and_filter,
                .entry_begin     = slot.entry_offsets[list_index] + static_cast<uint32_t>(begin),
                .entry_count     = job_entry_count,
                .command_offset  = command_count
            });
            m_gpu_job_infos.push_back(Gpu_job_info{
                .view        = static_cast<uint32_t>(view_index),
                .filter      = filter_index,
                .list_index  = static_cast<uint32_t>(list_index),
                .chunk_begin = static_cast<uint32_t>(begin),
                .selection   = selection
            });
            m_gpu_indirect_count_jobs.push_back(Indirect_count_draw_job{
                .command_offset = command_count,
                .max_draw_count = job_entry_count,
                .index_buffer   = index_buffer_index
            });
            command_count  += ((job_entry_count + command_alignment - 1) / command_alignment) * command_alignment;
            max_job_entries = std::max(max_job_entries, job_entry_count);
        }
    };

    if (shadow_selection) {
        ERHE_VERIFY(m_shadow_selection_primitive_buffer != nullptr);
        const std::size_t max_per_chunk = get_records_per_chunk(*m_shadow_selection_primitive_buffer);
        const uint32_t    filter_index  = get_gpu_job_filter_index(m_shadow_selection_filter);
        for (std::size_t view_index = 0; view_index < view_count; ++view_index) {
            if (m_cull_views[view_index].purpose != Draw_purpose::shadow) {
                continue;
            }
            // The view's jobs are consecutive (one shadow pass draws them).
            m_gpu_shadow_view_jobs[view_index].first_job = static_cast<uint32_t>(m_gpu_jobs.size());
            for (std::size_t list_index = 0; list_index < list_count; ++list_index) {
                const Draw_list& draw_list = slot.lists[list_index];
                if (
                    (draw_list.key.purpose != Draw_purpose::shadow) ||
                    draw_list.entries.empty() ||
                    !is_layer_selected(m_shadow_selection_layers, draw_list.key.layer_id)
                ) {
                    continue;
                }
                m_gpu_shadow_job_lookup[(view_index * list_count) + list_index] = static_cast<uint32_t>(m_gpu_jobs.size());
                add_list_jobs(list_index, view_index, filter_index, max_per_chunk, Command_selection_id::none);
            }
            m_gpu_shadow_view_jobs[view_index].job_count = static_cast<uint32_t>(m_gpu_jobs.size()) - m_gpu_shadow_view_jobs[view_index].first_job;
        }
    }

    // The color selections' jobs: one consecutive run, the frame's color set.
    m_gpu_color_jobs = Gpu_job_range{.first_job = static_cast<uint32_t>(m_gpu_jobs.size()), .job_count = 0};
    for (std::size_t selection_index = 0, selection_end = m_color_selections.size(); selection_index < selection_end; ++selection_index) {
        const Color_selection_record& selection = m_color_selections[selection_index];
        const std::span<const erhe::scene::Layer_id> layers{m_color_selection_layers.data() + selection.layer_begin, selection.layer_count};
        const std::size_t max_per_chunk = get_records_per_chunk(*selection.primitive_buffer);
        const uint32_t    filter_index  = get_gpu_job_filter_index(selection.filter);
        const std::size_t view_index    = static_cast<std::size_t>(selection.cull_view);
        ERHE_VERIFY(view_index < view_count);
        for (const erhe::primitive::Blending_class blending : erhe::primitive::blending_class_draw_order) {
            if (!is_blending_class_selected(selection.blending, blending)) {
                continue;
            }
            for (std::size_t list_index = 0; list_index < list_count; ++list_index) {
                const Draw_list& draw_list = slot.lists[list_index];
                if (
                    (draw_list.key.purpose != Draw_purpose::color) ||
                    (draw_list.key.blending != blending) ||
                    draw_list.entries.empty() ||
                    !is_layer_selected(layers, draw_list.key.layer_id)
                ) {
                    continue;
                }
                m_gpu_color_job_lookup[(selection_index * list_count) + list_index] = static_cast<uint32_t>(m_gpu_jobs.size());
                add_list_jobs(list_index, view_index, filter_index, max_per_chunk, static_cast<Command_selection_id>(selection_index));
            }
        }
    }
    m_gpu_color_jobs.job_count = static_cast<uint32_t>(m_gpu_jobs.size()) - m_gpu_color_jobs.first_job;
    if (m_gpu_jobs.empty()) {
        return;
    }

    Gpu_cull_classifier& classifier = *m_gpu_cull_classifier;
    Device&              device     = classifier.get_device();
    if (!m_gpu_command_timer) {
        m_gpu_command_timer = std::make_unique<Gpu_timer>(device, "Cull command generation");
    }

    constexpr std::size_t command_stride = sizeof(Draw_indexed_primitives_indirect_command);
    const std::size_t     job_count      = m_gpu_jobs.size();
    const std::size_t     job_bytes      = classifier.get_job_data_offset() + (job_count * Gpu_cull_classifier::job_bytes);
    const std::size_t     command_bytes  = static_cast<std::size_t>(command_count) * command_stride;
    const std::size_t     count_bytes    = job_count * Gpu_cull_classifier::count_bytes_per_job;
    // Pass words, then as many filter words (cull_compact.comp).
    const uint32_t        pass_word_count = command_count / command_alignment;
    const std::size_t     bits_bytes      = 2 * static_cast<std::size_t>(pass_word_count) * sizeof(uint32_t);
    // The dispatches' workgroups, flat over (job, tile) and over jobs, split
    // into y past the device's x limit (cull_compact.comp
    // flat_workgroup_index()).
    const std::size_t     group_size              = Gpu_cull_classifier::compact_workgroup_size;
    const uint32_t        bits_workgroups_per_job = static_cast<uint32_t>((max_job_entries + group_size - 1) / group_size);
    const auto            split_workgroups = [&device](const std::size_t workgroup_count) -> std::array<std::uintptr_t, 2> {
        const std::size_t max_x = static_cast<std::size_t>(device.get_info().max_compute_workgroup_count[0]);
        const std::size_t max_y = static_cast<std::size_t>(device.get_info().max_compute_workgroup_count[1]);
        ERHE_VERIFY((max_x > 0) && (max_y > 0));
        const std::size_t x = std::min(workgroup_count, max_x);
        const std::size_t y = (workgroup_count + x - 1) / x;
        ERHE_VERIFY(y <= max_y);
        return std::array<std::uintptr_t, 2>{static_cast<std::uintptr_t>(x), static_cast<std::uintptr_t>(y)};
    };
    const std::size_t     template_bytes = static_cast<std::size_t>(slot.entry_count) * Gpu_cull_classifier::template_bytes_per_entry;
    ERHE_VERIFY(slot.gpu_templates && (slot.gpu_bounds_capacity >= template_bytes));

    // The job table, the header and the filter table (cull_compact.comp
    // layout; each filter an erhe::Item_filter as 32-bit halves).
    Ring_buffer_range job_range = classifier.get_job_buffer().acquire(Ring_buffer_usage::CPU_write, job_bytes);
    {
        const std::span<std::byte> gpu_data = job_range.get_span();
        const glm::uvec4 header{
            static_cast<uint32_t>(job_count),
            slot.entry_count,
            pass_word_count,
            bits_workgroups_per_job
        };
        std::memcpy(gpu_data.data() + classifier.get_job_header_offset(), &header, sizeof(header));
        for (std::size_t filter_index = 0, filter_end = m_gpu_job_filters.size(); filter_index < filter_end; ++filter_index) {
            const erhe::Item_filter&        filter    = m_gpu_job_filters[filter_index];
            const std::array<uint32_t, 2>   all_set   = Gpu_cull_classifier::split_uint64(filter.require_all_bits_set);
            const std::array<uint32_t, 2>   one_set   = Gpu_cull_classifier::split_uint64(filter.require_at_least_one_bit_set);
            const std::array<uint32_t, 2>   all_clear = Gpu_cull_classifier::split_uint64(filter.require_all_bits_clear);
            const std::array<uint32_t, 2>   one_clear = Gpu_cull_classifier::split_uint64(filter.require_at_least_one_bit_clear);
            const std::array<glm::uvec4, 2> filter_words{
                glm::uvec4{all_set  [0], all_set  [1], one_set  [0], one_set  [1]},
                glm::uvec4{all_clear[0], all_clear[1], one_clear[0], one_clear[1]}
            };
            std::memcpy(gpu_data.data() + classifier.get_job_filter_offset() + (filter_index * sizeof(filter_words)), filter_words.data(), sizeof(filter_words));
        }
        std::memcpy(gpu_data.data() + classifier.get_job_data_offset(), m_gpu_jobs.data(), job_count * Gpu_cull_classifier::job_bytes);
        job_range.bytes_written(job_bytes);
        job_range.close();
    }

    // The frame's commands and counts: device memory, released to its ring
    // right after the dispatch (kept until the frame retired); the frame's
    // shadow passes draw from them.
    Ring_buffer_range command_range = classifier.get_command_buffer().acquire(Ring_buffer_usage::GPU_access, command_bytes);
    command_range.bytes_gpu_used(command_bytes);
    command_range.close();
    Ring_buffer_range count_range = classifier.get_count_buffer().acquire(Ring_buffer_usage::GPU_access, count_bytes);
    count_range.bytes_gpu_used(count_bytes);
    count_range.close();
    Ring_buffer_range bits_range = classifier.get_bits_buffer().acquire(Ring_buffer_usage::GPU_access, bits_bytes);
    bits_range.bytes_gpu_used(bits_bytes);
    bits_range.close();
    const Buffer* const command_buffer_object = command_range.get_buffer()->get_buffer();
    const Buffer* const count_buffer_object   = count_range.get_buffer()->get_buffer();
    m_gpu_command_buffer      = command_buffer_object;
    m_gpu_command_byte_offset = command_range.get_byte_start_offset_in_buffer();
    m_gpu_count_buffer        = count_buffer_object;
    m_gpu_count_byte_offset   = count_range.get_byte_start_offset_in_buffer();

    // Classification writes -> pass bit reads; pass bits -> compaction.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_storage_barrier_bit);
    {
        const Scoped_gpu_timer timer_scope{*m_gpu_command_timer, command_buffer};
        const auto bind_buffers = [&](Compute_command_encoder& encoder) {
            encoder.set_buffer(Buffer_target::storage, slot.gpu_templates.get(), 0, template_bytes, Gpu_cull_classifier::compact_templates_binding_point);
            classifier.get_job_buffer().bind(encoder, job_range);
            encoder.set_buffer(Buffer_target::storage, &mask_buffer, mask_byte_offset, mask_byte_count, Gpu_cull_classifier::compact_masks_binding_point);
            encoder.set_buffer(Buffer_target::storage, command_buffer_object, m_gpu_command_byte_offset, command_bytes, Gpu_cull_classifier::compact_commands_binding_point);
            encoder.set_buffer(Buffer_target::storage, count_buffer_object, m_gpu_count_byte_offset, count_bytes, Gpu_cull_classifier::compact_counts_binding_point);
            encoder.set_buffer(Buffer_target::storage, bits_range.get_buffer()->get_buffer(), bits_range.get_byte_start_offset_in_buffer(), bits_bytes, Gpu_cull_classifier::compact_bits_binding_point);
        };
        {
            // Pass bits: one invocation per (job, entry).
            Compute_command_encoder encoder = device.make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(classifier.get_compact_bind_group_layout());
            encoder.set_compute_pipeline(classifier.get_compact_bits_pipeline());
            bind_buffers(encoder);
            const std::array<std::uintptr_t, 2> groups = split_workgroups(job_count * bits_workgroups_per_job);
            encoder.dispatch_compute(groups[0], groups[1], 1);
        }
        command_buffer.memory_barrier(Memory_barrier_mask::shader_storage_barrier_bit);
        {
            // Compaction: one workgroup per job.
            Compute_command_encoder encoder = device.make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(classifier.get_compact_bind_group_layout());
            encoder.set_compute_pipeline(classifier.get_compact_pipeline());
            bind_buffers(encoder);
            const std::array<std::uintptr_t, 2> groups = split_workgroups(job_count);
            encoder.dispatch_compute(groups[0], groups[1], 1);
        }
    }
    // Compaction writes -> the passes' indirect command and count reads, the
    // prepare steps' reads (Metal's encoding kernel; each shadow pass
    // prepares its jobs, prepare_gpu_shadow_pass(), the color jobs are
    // prepared below) and the count (and the check's) copies below.
    command_buffer.memory_barrier(
        Memory_barrier_mask::command_barrier_bit |
        Memory_barrier_mask::shader_storage_barrier_bit |
        Memory_barrier_mask::pixel_buffer_barrier_bit
    );

    // The color jobs: one set for every color pass of the frame, prepared
    // here, outside any render pass (a view's color passes draw inside one
    // render pass, which no prepare may interrupt), rebased to the first
    // color job's command and count.
    m_gpu_color_indirect_count_draw_set = Indirect_count_draw_set{};
    if (m_gpu_color_jobs.job_count > 0) {
        const uint32_t first_command = m_gpu_indirect_count_jobs[m_gpu_color_jobs.first_job].command_offset;
        m_gpu_color_indirect_count_jobs.clear();
        for (uint32_t job = m_gpu_color_jobs.first_job, end = m_gpu_color_jobs.first_job + m_gpu_color_jobs.job_count; job < end; ++job) {
            const Indirect_count_draw_job& frame_job = m_gpu_indirect_count_jobs[job];
            ERHE_VERIFY(frame_job.command_offset >= first_command);
            m_gpu_color_indirect_count_jobs.push_back(
                Indirect_count_draw_job{
                    .command_offset = frame_job.command_offset - first_command,
                    .max_draw_count = frame_job.max_draw_count,
                    .index_buffer   = frame_job.index_buffer
                }
            );
        }
        if (!m_gpu_color_indirect_count_draws) {
            m_gpu_color_indirect_count_draws = std::make_unique<Indirect_count_draws>(device, erhe::utility::Debug_label{"Color GPU commands"});
        }
        Compute_command_encoder encoder = device.make_compute_command_encoder(command_buffer);
        m_gpu_color_indirect_count_draw_set = encoder.prepare_indexed_indirect_count_draws(
            *m_gpu_color_indirect_count_draws,
            Indirect_count_draws_prepare_info{
                .frame_index         = frame_index,
                .primitive_type      = Primitive_type::triangle,
                .index_buffers       = m_gpu_indirect_count_index_buffers,
                .command_buffer      = m_gpu_command_buffer,
                .command_byte_offset = m_gpu_command_byte_offset + (static_cast<std::size_t>(first_command) * command_stride),
                .count_buffer        = m_gpu_count_buffer,
                .count_byte_offset   = m_gpu_count_byte_offset + (static_cast<std::size_t>(m_gpu_color_jobs.first_job) * Gpu_cull_classifier::count_bytes_per_job),
                .count_byte_stride   = Gpu_cull_classifier::count_bytes_per_job,
                .jobs                = m_gpu_color_indirect_count_jobs
            }
        );
    }

    // The frame's counts, for the pass statistics: copied to host memory,
    // decoded by the classification of the frame that reuses this frame
    // slot (decode_retired_gpu_counts(); the frame has retired then).
    {
        const std::size_t slot_count = m_draw_list_slots.size();
        if (m_gpu_count_readbacks.size() != slot_count) {
            // Sized once: the device's frames in flight (publish()).
            ERHE_VERIFY(m_gpu_count_readbacks.empty());
            m_gpu_count_readbacks.resize(slot_count);
        }
        Gpu_count_readback& readback = m_gpu_count_readbacks[frame_index % slot_count];
        // The slot's previous readback was decoded at this frame's
        // classification, so its buffer is free to write (and to regrow).
        ERHE_VERIFY(!readback.pending);
        if (!readback.buffer || (readback.buffer->get_capacity_byte_count() < count_bytes)) {
            // A multiple of every nonCoherentAtomSize, so the whole buffer
            // can be invalidated at the decode.
            constexpr std::size_t min_capacity = 256;
            const std::size_t     capacity     = std::bit_ceil(std::max(count_bytes, min_capacity));
            readback.buffer = std::make_unique<Buffer>(
                device,
                Buffer_create_info{
                    .capacity_byte_count                    = capacity,
                    .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                    .usage                                  = Buffer_usage::transfer_dst,
                    .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
                    .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
                    .debug_label                            = erhe::utility::Debug_label{"Draw_list_scene GPU command count readback"}
                }
            );
        }
        {
            Blit_command_encoder blit = device.make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(count_buffer_object, m_gpu_count_byte_offset, readback.buffer.get(), 0, count_bytes);
        }
        readback.byte_count  = count_bytes;
        readback.frame_index = frame_index;
        readback.view_count  = view_count;
        readback.job_views.clear();
        readback.job_selections.clear();
        for (const Gpu_job_info& info : m_gpu_job_infos) {
            readback.job_views.push_back(info.view);
            readback.job_selections.push_back(info.selection);
        }
        readback.selection_keys.clear();
        for (const Color_selection_record& selection : m_color_selections) {
            readback.selection_keys.push_back(selection.key);
        }
        readback.pending = true;
    }

    if (check_generation.has_value()) {
        record_gpu_command_check(frame_index, command_buffer, check_generation.value(), command_count, command_bytes, count_bytes);
    }

    job_range.release();
    command_range.release();
    count_range.release();
    bits_range.release();
}

void Draw_list_scene::decode_retired_gpu_counts(const uint64_t frame_index)
{
    if (m_gpu_count_readbacks.empty()) {
        return;
    }
    Gpu_count_readback& readback = m_gpu_count_readbacks[frame_index % m_gpu_count_readbacks.size()];
    if (!readback.pending) {
        return;
    }
    // This frame reuses the readback frame's slot: that frame retired
    // (Device::acquire_frame_slot() waited for it).
    ERHE_VERIFY((readback.frame_index + m_gpu_count_readbacks.size()) <= frame_index);
    ERHE_PROFILE_FUNCTION();
    const std::size_t          capacity = readback.buffer->get_capacity_byte_count();
    const std::span<std::byte> data     = readback.buffer->map_bytes(0, capacity);
    readback.buffer->invalidate(0, capacity);
    ERHE_VERIFY(readback.byte_count == (readback.job_views.size() * Gpu_cull_classifier::count_bytes_per_job));
    ERHE_VERIFY(data.size_bytes() >= readback.byte_count);
    m_retired_gpu_shadow_view_counts.assign(readback.view_count, Gpu_shadow_view_counts{});
    m_retired_gpu_color_counts.clear();
    for (const Gpu_command_key& key : readback.selection_keys) {
        m_retired_gpu_color_counts.push_back(Retired_color_selection_counts{.key = key});
    }
    for (std::size_t job_index = 0, job_end = readback.job_views.size(); job_index < job_end; ++job_index) {
        std::array<uint32_t, 2> counts{0u, 0u};
        std::memcpy(counts.data(), data.data() + (job_index * Gpu_cull_classifier::count_bytes_per_job), sizeof(counts));
        const uint32_t view = readback.job_views[job_index];
        ERHE_VERIFY(view < m_retired_gpu_shadow_view_counts.size());
        m_retired_gpu_shadow_view_counts[view].drawn    += counts[0];
        m_retired_gpu_shadow_view_counts[view].filtered += counts[1];
        const Command_selection_id selection = readback.job_selections[job_index];
        if (selection != Command_selection_id::none) {
            const std::size_t selection_index = static_cast<std::size_t>(selection);
            ERHE_VERIFY(selection_index < m_retired_gpu_color_counts.size());
            m_retired_gpu_color_counts[selection_index].drawn    += counts[0];
            m_retired_gpu_color_counts[selection_index].filtered += counts[1];
        }
    }
    m_retired_gpu_shadow_counts = Retired_gpu_shadow_counts{
        .frame_index = readback.frame_index,
        .views       = std::span<const Gpu_shadow_view_counts>{m_retired_gpu_shadow_view_counts}
    };
    m_has_retired_gpu_shadow_counts = true;
    readback.buffer->unmap();
    readback.pending = false;
}

void Draw_list_scene::record_gpu_command_check(
    const uint64_t                  frame_index,
    erhe::graphics::Command_buffer& command_buffer,
    const uint64_t                  check_generation,
    const uint32_t                  command_count,
    const std::size_t               command_bytes,
    const std::size_t               count_bytes
)
{
    using namespace erhe::graphics;

    // A diagnostic path: it may allocate. The GPU's counts and commands are
    // read back once the frame retired; what the CPU writer produces for
    // each job is captured now, from the frame's CPU masks.
    Gpu_cull_classifier& classifier = *m_gpu_cull_classifier;
    Device&              device     = classifier.get_device();
    const auto read_back = [&](const Buffer* source, const std::size_t source_offset, const std::size_t byte_count, std::vector<std::byte> Gpu_cull_check::* target) {
        std::shared_ptr<Ring_buffer_range> readback = std::make_shared<Ring_buffer_range>(
            classifier.get_readback_buffer().acquire(Ring_buffer_usage::CPU_read, byte_count)
        );
        {
            Blit_command_encoder blit = device.make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(
                source, source_offset,
                readback->get_buffer()->get_buffer(), readback->get_byte_start_offset_in_buffer(),
                byte_count
            );
        }
        device.add_completion_handler(
            [check = m_gpu_cull_check, readback, check_generation, target]() {
                const std::span<std::byte> gpu_data = readback->get_span();
                {
                    const std::lock_guard<std::mutex> lock{check->mutex};
                    if ((check->generation == check_generation) && (check->stage == Gpu_cull_check::Stage::recording)) {
                        ((*check).*target).assign(gpu_data.begin(), gpu_data.end());
                        --check->pending_reads;
                        check->complete_if_ready();
                    }
                }
                readback->bytes_gpu_used(gpu_data.size_bytes());
                readback->close();
                readback->release();
            }
        );
    };
    {
        const std::lock_guard<std::mutex> lock{m_gpu_cull_check->mutex};
        m_gpu_cull_check->pending_reads += 2;
    }
    read_back(m_gpu_count_buffer,   m_gpu_count_byte_offset,   count_bytes,   &Gpu_cull_check::gpu_counts);
    read_back(m_gpu_command_buffer, m_gpu_command_byte_offset, command_bytes, &Gpu_cull_check::gpu_commands);

    const Draw_list_slot& slot       = get_published_slot(frame_index);
    const std::size_t     list_count = slot.lists.size();

    const std::lock_guard<std::mutex> lock{m_gpu_cull_check->mutex};
    Gpu_cull_check& check = *m_gpu_cull_check;
    if ((check.generation != check_generation) || (check.stage != Gpu_cull_check::Stage::recording)) {
        return;
    }
    check.command_jobs.clear();
    check.command_candidates.clear();
    for (std::size_t job_index = 0, job_end = m_gpu_jobs.size(); job_index < job_end; ++job_index) {
        const Gpu_cull_job&          job              = m_gpu_jobs[job_index];
        const Gpu_job_info&          info             = m_gpu_job_infos[job_index];
        ERHE_VERIFY(info.list_index < list_count);
        const Draw_list&             draw_list        = slot.lists[info.list_index];
        const std::size_t            list_entry_count = draw_list.entries.size();
        ERHE_VERIFY(info.list_index < m_cull_masks.size());
        const std::vector<uint64_t>& masks            = m_cull_masks[info.list_index];
        const std::size_t            group            = info.view / 64;
        const uint64_t               view_bit         = uint64_t{1} << (info.view % 64);
        ERHE_VERIFY(masks.size() >= ((group + 1) * list_entry_count));
        ERHE_VERIFY(job.entry_begin == (slot.entry_offsets[info.list_index] + info.chunk_begin));
        ERHE_VERIFY(info.filter < m_gpu_job_filters.size());
        const erhe::Item_filter& filter = m_gpu_job_filters[info.filter];
        Gpu_cull_check::Command_job record{
            .view           = info.view,
            .list_index     = info.list_index,
            .chunk_begin    = info.chunk_begin,
            .entry_count    = job.entry_count,
            .command_offset = job.command_offset,
            .color          = (info.selection != Command_selection_id::none),
            .candidate_begin = static_cast<uint32_t>(check.command_candidates.size()),
            .candidate_count = 0
        };
        for (uint32_t local = 0; local < job.entry_count; ++local) {
            const std::size_t      entry_index = static_cast<std::size_t>(info.chunk_begin) + local;
            const Draw_list_entry& entry       = draw_list.entries[entry_index];
            const bool             in_view     = ((masks[(group * list_entry_count) + entry_index] & view_bit) != 0);
            if (!in_view || !filter(entry.flag_bits)) {
                continue;
            }
            check.command_candidates.push_back(Gpu_cull_check::Command_candidate{
                .entry   = job.entry_begin + local,
                .command = Draw_indexed_primitives_indirect_command{
                    entry.index_count,
                    1u,
                    entry.first_index,
                    entry.base_vertex,
                    local
                }
            });
            ++record.candidate_count;
        }
        check.command_jobs.push_back(record);
    }
    ERHE_VERIFY(check.command_jobs.size() == m_gpu_jobs.size());
    ERHE_VERIFY(check.command_candidates.size() <= command_count);
}

void Draw_list_scene::prepare_gpu_shadow_pass(
    erhe::graphics::Command_buffer& command_buffer,
    const Cull_view                 cull_view,
    const uint64_t                  frame_index
)
{
    using namespace erhe::graphics;

    ERHE_PROFILE_FUNCTION();
    if (!has_gpu_shadow_commands(frame_index) || (cull_view == Cull_view::none)) {
        return;
    }
    const std::size_t view_index = static_cast<std::size_t>(cull_view);
    ERHE_VERIFY(view_index < m_gpu_shadow_view_jobs.size());
    const Gpu_job_range& view_jobs = m_gpu_shadow_view_jobs[view_index];
    m_gpu_pass_cull_view = cull_view;
    m_gpu_pass_jobs      = view_jobs;
    m_gpu_indirect_count_draw_set = Indirect_count_draw_set{};
    if (view_jobs.job_count == 0) {
        return;
    }

    // The pass's jobs, rebased to the view's first command and count, so
    // the prepare (on Metal, the indirect command buffer) covers this pass
    // only. Persistent scratch, capacity kept.
    constexpr std::size_t command_stride = sizeof(Draw_indexed_primitives_indirect_command);
    const uint32_t        first_command  = m_gpu_indirect_count_jobs[view_jobs.first_job].command_offset;
    m_gpu_pass_indirect_count_jobs.clear();
    for (uint32_t job = view_jobs.first_job, end = view_jobs.first_job + view_jobs.job_count; job < end; ++job) {
        const Indirect_count_draw_job& frame_job = m_gpu_indirect_count_jobs[job];
        ERHE_VERIFY(frame_job.command_offset >= first_command);
        m_gpu_pass_indirect_count_jobs.push_back(
            Indirect_count_draw_job{
                .command_offset = frame_job.command_offset - first_command,
                .max_draw_count = frame_job.max_draw_count,
                .index_buffer   = frame_job.index_buffer
            }
        );
    }

    Device& device = m_gpu_cull_classifier->get_device();
    if (!m_gpu_indirect_count_draws) {
        m_gpu_indirect_count_draws = std::make_unique<Indirect_count_draws>(device, erhe::utility::Debug_label{"Shadow GPU commands"});
    }
    // Before the pass's render pass begins (no render encoder open): on
    // Metal the kernel encodes the pass's surviving commands into the
    // indirect command buffer, ordered after the frame's compaction and the
    // previous pass's draws by the command buffer's encoder fence; elsewhere
    // the prepare only records the parameters.
    Compute_command_encoder encoder = device.make_compute_command_encoder(command_buffer);
    m_gpu_indirect_count_draw_set = encoder.prepare_indexed_indirect_count_draws(
        *m_gpu_indirect_count_draws,
        Indirect_count_draws_prepare_info{
            .frame_index         = frame_index,
            .primitive_type      = Primitive_type::triangle,
            .index_buffers       = m_gpu_indirect_count_index_buffers,
            .command_buffer      = m_gpu_command_buffer,
            .command_byte_offset = m_gpu_command_byte_offset + (static_cast<std::size_t>(first_command) * command_stride),
            .count_buffer        = m_gpu_count_buffer,
            .count_byte_offset   = m_gpu_count_byte_offset + (static_cast<std::size_t>(view_jobs.first_job) * Gpu_cull_classifier::count_bytes_per_job),
            .count_byte_stride   = Gpu_cull_classifier::count_bytes_per_job,
            .jobs                = m_gpu_pass_indirect_count_jobs
        }
    );
}

auto Draw_list_scene::get_gpu_job_filter_index(const erhe::Item_filter& filter) -> uint32_t
{
    for (std::size_t i = 0, end = m_gpu_job_filters.size(); i < end; ++i) {
        if (m_gpu_job_filters[i] == filter) {
            return static_cast<uint32_t>(i);
        }
    }
    // The job table holds a fixed number of filters (cull_compact.comp).
    ERHE_VERIFY(m_gpu_job_filters.size() < Gpu_cull_classifier::max_job_filters);
    m_gpu_job_filters.push_back(filter);
    return static_cast<uint32_t>(m_gpu_job_filters.size() - 1);
}

// --- Raster occlusion test (doc/plans/occlusion_culling.md) ---------------

auto Draw_list_scene::add_occlusion_view(
    const uint64_t         frame_index,
    const Cull_view        cull_view,
    const glm::vec4&       near_plane,
    const Gpu_command_key& key
) -> Cull_view
{
    assert_render_phase();
    if (cull_view == Cull_view::none) {
        return Cull_view::none;
    }
    // Given with the frame's views, before their classification.
    ERHE_VERIFY(m_cull_frame_started && (m_cull_frame_index == frame_index) && !m_cull_frame_classified);
    ERHE_VERIFY(static_cast<std::size_t>(cull_view) < m_cull_views.size());
    ERHE_VERIFY(m_cull_views[static_cast<std::size_t>(cull_view)].purpose == Draw_purpose::color);
    const Draw_list_slot& slot = get_published_slot(frame_index);
    if ((slot.classification != Cull_classification::gpu) || (slot.occlusion != Occlusion_culling::on)) {
        return Cull_view::none;
    }
    for (const Occlusion_view_record& record : m_occlusion_views) {
        if ((record.cull_view == cull_view) && (record.key == key)) {
            return cull_view;
        }
    }
    m_occlusion_views.push_back(Occlusion_view_record{.cull_view = cull_view, .near_plane = near_plane, .key = key});
    return cull_view;
}

void Draw_list_scene::prepare_occlusion_slot(
    const uint64_t    frame_index,
    const std::size_t entry_count,
    const std::size_t       group_count,
    const Occlusion_culling occlusion
)
{
    using namespace erhe::graphics;

    if (m_occlusion_slots.size() != m_draw_list_slots.size()) {
        // Sized once: the device's frames in flight (publish()).
        ERHE_VERIFY(m_occlusion_slots.empty());
        m_occlusion_slots.resize(m_draw_list_slots.size());
    }
    Occlusion_slot& slot = m_occlusion_slots[frame_index % m_occlusion_slots.size()];
    // The slot's previous frame was decoded at this frame's classification
    // (decode_retired_occlusion() ran first), so the buffer is free.
    ERHE_VERIFY(!slot.pending);
    Device&           device       = m_gpu_cull_classifier->get_device();
    const std::size_t alignment    = std::max(c_occlusion_region_alignment, static_cast<std::size_t>(device.get_info().shader_storage_buffer_offset_alignment));
    const std::size_t region_bytes = (occlusion == Occlusion_culling::on) ? (group_count * entry_count * Gpu_cull_classifier::mask_bytes_per_entry_group) : 0;
    const std::size_t bind_bytes   = std::max(region_bytes, c_occlusion_min_region_bytes);
    // Three regions (test, result, culled), each at a multiple of the
    // storage binding alignment.
    const std::size_t stride       = align_up(bind_bytes, alignment);
    const std::size_t needed       = (2 * stride) + bind_bytes;
    if (!m_occlusion_no_previous_buffer) {
        // Bound for the previous frame's words when the classification
        // consumes none (the shader does not read it then).
        m_occlusion_no_previous_buffer = std::make_unique<Buffer>(
            device,
            Buffer_create_info{
                .capacity_byte_count = c_occlusion_min_region_bytes,
                .usage               = Buffer_usage::storage,
                .debug_label         = erhe::utility::Debug_label{"Draw_list_scene no previous occlusion words"}
            }
        );
    }
    if (!slot.buffer || (slot.buffer->get_capacity_byte_count() < needed)) {
        // Rare: the slot's high-water size (lists, views) grew.
        const std::size_t capacity = std::bit_ceil(needed);
        slot.buffer = std::make_unique<Buffer>(
            device,
            Buffer_create_info{
                .capacity_byte_count                    = capacity,
                .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = Buffer_usage::storage,
                .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
                .debug_label                            = erhe::utility::Debug_label{"Draw_list_scene occlusion words"}
            }
        );
        log_draw_list->info("Occlusion words: frame slot {} buffer {} bytes ({} entries, {} view groups)", frame_index % m_occlusion_slots.size(), capacity, entry_count, group_count);
    }
    if ((occlusion == Occlusion_culling::on) && (slot.index_capacity < entry_count)) {
        // The occlusion pass's indices (occlusion_test.vert): per entry e
        // the three template faces +X (corners 1 3 7 5), +Y (2 6 7 3) and
        // +Z (4 5 7 6), template vertex = corner - 1, at 7 e. Written once
        // per capacity; grown rarely (the slot's high-water entry count).
        static constexpr std::array<uint32_t, Gpu_cull_classifier::occlusion_indices_per_entry> pattern{
            0u, 2u, 6u,  6u, 4u, 0u, // +X
            1u, 5u, 6u,  6u, 2u, 1u, // +Y
            3u, 4u, 6u,  6u, 5u, 3u  // +Z
        };
        const std::size_t capacity   = std::bit_ceil(std::max(entry_count, std::size_t{64}));
        const std::size_t byte_count = capacity * pattern.size() * sizeof(uint32_t);
        slot.index_buffer = std::make_unique<Buffer>(
            device,
            Buffer_create_info{
                .capacity_byte_count                    = byte_count,
                .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = Buffer_usage::index,
                .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
                .debug_label                            = erhe::utility::Debug_label{"Draw_list_scene occlusion box indices"}
            }
        );
        const std::span<std::byte> mapped = slot.index_buffer->map_bytes(0, byte_count);
        std::byte* write = mapped.data();
        for (std::size_t entry = 0; entry < capacity; ++entry) {
            const uint32_t base = static_cast<uint32_t>(entry * Gpu_cull_classifier::occlusion_vertices_per_entry);
            std::array<uint32_t, Gpu_cull_classifier::occlusion_indices_per_entry> indices{};
            for (std::size_t i = 0; i < pattern.size(); ++i) {
                indices[i] = base + pattern[i];
            }
            std::memcpy(write, indices.data(), sizeof(indices));
            write += sizeof(indices);
        }
        slot.index_buffer->flush_bytes(0, byte_count);
        slot.index_buffer->unmap();
        slot.index_capacity = capacity;
        log_draw_list->info("Occlusion box indices: frame slot {} for {} entries ({} bytes)", frame_index % m_occlusion_slots.size(), capacity, byte_count);
    }
    slot.region_bytes  = region_bytes;
    slot.bind_bytes    = bind_bytes;
    slot.result_offset = stride;
    slot.culled_offset = 2 * stride;
}

void Draw_list_scene::draw_occlusion(const Draw_occlusion_parameters& parameters)
{
    using namespace erhe::graphics;

    ERHE_PROFILE_FUNCTION();
    assert_render_phase();
    verify_cull_view_classified(parameters.frame_index, parameters.cull_view);
    // The frame's classification wrote the start words for the view.
    ERHE_VERIFY(m_occlusion_frame_active && (m_occlusion_frame_index == parameters.frame_index));
    ERHE_VERIFY(std::ranges::any_of(m_occlusion_views, [&parameters](const Occlusion_view_record& record) -> bool { return record.cull_view == parameters.cull_view; }));
    ERHE_VERIFY(parameters.render_pass != nullptr);
    const Draw_list_slot& slot           = get_published_slot(parameters.frame_index);
    const Occlusion_slot& occlusion_slot = m_occlusion_slots[parameters.frame_index % m_occlusion_slots.size()];
    ERHE_VERIFY(occlusion_slot.pending && (occlusion_slot.frame_index == parameters.frame_index));
    ERHE_VERIFY(occlusion_slot.entry_count == slot.entry_count);
    if (slot.entry_count == 0) {
        return;
    }

    Gpu_cull_classifier&    classifier = *m_gpu_cull_classifier;
    Render_command_encoder& encoder    = parameters.render_encoder;
    const Vertex_input_entry& empty_vertex_input = m_mesh_memory.get_empty_vertex_input();
    Render_pipeline* const pipeline = classifier.get_occlusion_pipeline().get_pipeline_for(
        parameters.render_pass->get_descriptor(),
        &Color_blend_state::color_writes_disabled,
        &classifier.get_occlusion_shader_stages(),
        empty_vertex_input.vertex_input.get(),
        &empty_vertex_input.vertex_format
    );
    if (pipeline == nullptr) {
        return;
    }

    const std::size_t view_index  = static_cast<std::size_t>(parameters.cull_view);
    const glm::uvec4  info{
        slot.entry_count,
        static_cast<uint32_t>(view_index / 64),
        static_cast<uint32_t>(view_index % 64),
        0u
    };
    Ring_buffer_range view_range = classifier.get_occlusion_view_buffer().acquire(Ring_buffer_usage::CPU_write, classifier.get_occlusion_view_block_bytes());
    {
        const std::span<std::byte> gpu_data = view_range.get_span();
        std::memcpy(gpu_data.data() + classifier.get_occlusion_clip_from_world_offset(), &parameters.clip_from_world, sizeof(glm::mat4));
        std::memcpy(gpu_data.data() + classifier.get_occlusion_eye_offset(),             &parameters.eye,             sizeof(glm::vec4));
        std::memcpy(gpu_data.data() + classifier.get_occlusion_info_offset(),            &info,                       sizeof(glm::uvec4));
        view_range.bytes_written(classifier.get_occlusion_view_block_bytes());
        view_range.close();
    }

    const std::size_t bounds_bytes = static_cast<std::size_t>(slot.entry_count) * Gpu_cull_classifier::bounds_bytes_per_entry;
    ERHE_VERIFY(slot.gpu_bounds && (slot.gpu_bounds_capacity >= bounds_bytes));
    encoder.set_bind_group_layout(classifier.get_occlusion_bind_group_layout());
    encoder.set_render_pipeline(*pipeline);
    encoder.set_viewport_rect(parameters.viewport.x, parameters.viewport.y, parameters.viewport.width, parameters.viewport.height);
    classifier.get_occlusion_view_buffer().bind(encoder, view_range);
    encoder.set_buffer(Buffer_target::storage, slot.gpu_bounds.get(),        0,                            bounds_bytes,                Gpu_cull_classifier::occlusion_bounds_binding_point);
    encoder.set_buffer(Buffer_target::storage, occlusion_slot.buffer.get(), 0,                            occlusion_slot.region_bytes, Gpu_cull_classifier::occlusion_draw_test_binding_point);
    encoder.set_buffer(Buffer_target::storage, occlusion_slot.buffer.get(), occlusion_slot.result_offset, occlusion_slot.region_bytes, Gpu_cull_classifier::occlusion_draw_result_binding_point);
    ERHE_VERIFY(occlusion_slot.index_buffer && (occlusion_slot.index_capacity >= slot.entry_count));
    encoder.set_index_buffer(occlusion_slot.index_buffer.get());
    encoder.draw_indexed_primitives(
        Primitive_type::triangle,
        static_cast<std::uintptr_t>(slot.entry_count) * Gpu_cull_classifier::occlusion_indices_per_entry,
        erhe::dataformat::Format::format_32_scalar_uint,
        0
    );
    view_range.release();
}

void Draw_list_scene::record_frame_end(const uint64_t frame_index, erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    assert_render_phase();
    if (!m_occlusion_frame_active || (m_occlusion_frame_index != frame_index)) {
        return;
    }
    const Occlusion_slot& slot = m_occlusion_slots[frame_index % m_occlusion_slots.size()];
    ERHE_VERIFY(slot.pending && (slot.frame_index == frame_index));
    // The classification's start words and the occlusion passes' fragment
    // atomics -> the CPU decode once the frame retired.
    command_buffer.buffer_barrier(
        *slot.buffer,
        Buffer_usage_flag_bit_mask::storage_write | Buffer_usage_flag_bit_mask::compute_shader | Buffer_usage_flag_bit_mask::fragment_shader,
        Buffer_usage_flag_bit_mask::host_read
    );
}

auto Draw_list_scene::get_retired_occlusion_counts(const Gpu_command_key& key) const -> std::optional<Retired_occlusion_counts>
{
    assert_render_phase();
    if (!m_has_retired_occlusion_counts) {
        return {};
    }
    for (const Retired_occlusion_view_counts& counts : m_retired_occlusion_counts) {
        if (counts.key == key) {
            return Retired_occlusion_counts{
                .frame_index = m_retired_occlusion_frame_index,
                .tested      = counts.tested,
                .occluded    = counts.occluded,
                .untested    = counts.untested,
                .culled      = counts.culled
            };
        }
    }
    return {};
}

void Draw_list_scene::capture_occlusion_check(const uint64_t frame_index)
{
    // A diagnostic path: it may allocate.
    const std::lock_guard<std::mutex> lock{m_occlusion_check->mutex};
    Gpu_occlusion_check& check = *m_occlusion_check;
    if (check.stage != Gpu_occlusion_check::Stage::requested) {
        return;
    }
    ERHE_PROFILE_FUNCTION();
    const Draw_list_slot& slot        = get_published_slot(frame_index);
    const std::size_t     list_count  = slot.lists.size();
    check.stage       = Gpu_occlusion_check::Stage::recording;
    check.frame_index = frame_index;
    check.entry_count = slot.entry_count;
    check.view_count  = static_cast<uint32_t>(m_cull_views.size());
    check.list_entry_offsets.assign(slot.entry_offsets.begin(), slot.entry_offsets.end());
    check.entry_objects.assign(slot.entry_count, uint32_t{0});
    for (std::size_t list_index = 0; list_index < list_count; ++list_index) {
        const Draw_list& draw_list = slot.lists[list_index];
        for (std::size_t entry = 0, end = draw_list.entries.size(); entry < end; ++entry) {
            check.entry_objects[slot.entry_offsets[list_index] + entry] = draw_list.entries[entry].object_index;
        }
    }
    // Per view, from the frame's CPU masks: the bounded color entries in
    // the frustum, and those of them whose box (not grown) intersects or
    // crosses the near plane - the classification marks them visible
    // without a test (it tests the grown box, which reaches the plane
    // whenever the box does).
    check.views.clear();
    for (const Occlusion_view_record& record : m_occlusion_views) {
        Gpu_occlusion_check::View& view = check.views.emplace_back();
        view.cull_view = record.cull_view;
        view.key       = record.key;
        const std::size_t view_index = static_cast<std::size_t>(record.cull_view);
        const std::size_t group      = view_index / 64;
        const uint64_t    view_bit   = uint64_t{1} << (view_index % 64);
        const glm::vec3   normal{record.near_plane};
        for (std::size_t list_index = 0; list_index < list_count; ++list_index) {
            const Draw_list& draw_list = slot.lists[list_index];
            if ((draw_list.key.purpose != Draw_purpose::color) || (draw_list.key.mobility == Draw_mobility::skinned)) {
                continue;
            }
            const std::size_t list_entry_count = draw_list.entries.size();
            if ((list_entry_count == 0) || (list_index >= m_cull_masks.size())) {
                continue;
            }
            const std::vector<uint64_t>& masks  = m_cull_masks[list_index];
            const Draw_list_bounds&      bounds = draw_list.bounds;
            ERHE_VERIFY(masks.size() >= ((group + 1) * list_entry_count));
            for (std::size_t entry = 0; entry < list_entry_count; ++entry) {
                const glm::vec3 extent{bounds.extent_x[entry], bounds.extent_y[entry], bounds.extent_z[entry]};
                if (!(extent.x >= 0.0f) || ((masks[(group * list_entry_count) + entry] & view_bit) == 0)) {
                    continue;
                }
                ++view.bounded_in_frustum;
                const glm::vec3 center{bounds.center_x[entry], bounds.center_y[entry], bounds.center_z[entry]};
                const float     near_distance = glm::dot(normal, center) + record.near_plane.w - glm::dot(glm::abs(normal), extent);
                if (near_distance < 0.0f) {
                    view.near_plane_entries.push_back(slot.entry_offsets[list_index] + static_cast<uint32_t>(entry));
                }
            }
        }
    }
}

void Draw_list_scene::decode_retired_occlusion(const uint64_t frame_index)
{
    if (m_occlusion_slots.empty()) {
        return;
    }
    Occlusion_slot& slot = m_occlusion_slots[frame_index % m_occlusion_slots.size()];
    if (!slot.pending) {
        return;
    }
    // This frame reuses the slot of the frame that wrote the words: that
    // frame retired (Device::acquire_frame_slot() waited for it).
    ERHE_VERIFY((slot.frame_index + m_occlusion_slots.size()) <= frame_index);
    ERHE_PROFILE_FUNCTION();
    const std::size_t          capacity = slot.buffer->get_capacity_byte_count();
    const std::span<std::byte> data     = slot.buffer->map_bytes(0, capacity);
    slot.buffer->invalidate(0, capacity);
    ERHE_VERIFY(data.size_bytes() >= (slot.culled_offset + slot.region_bytes));
    const std::size_t entry_count = slot.entry_count;
    // The words of (group, entry), bit = view % 64 (cull_classify.comp).
    const auto read_bit = [&data, entry_count](const std::size_t region_offset, const std::size_t view, const std::size_t entry) -> bool {
        const std::size_t group = view / 64;
        const std::size_t bit   = view % 64;
        uint32_t word = 0;
        const std::size_t byte_offset = region_offset + ((((group * entry_count) + entry) * 2) + (bit / 32)) * sizeof(uint32_t);
        std::memcpy(&word, data.data() + byte_offset, sizeof(word));
        return ((word >> (bit % 32)) & 1u) != 0;
    };

    m_retired_occlusion_counts.clear();
    for (const Occlusion_view_record& record : slot.views) {
        const std::size_t view = static_cast<std::size_t>(record.cull_view);
        Retired_occlusion_view_counts counts{.key = record.key};
        for (std::size_t entry = 0; entry < entry_count; ++entry) {
            const bool tested  = read_bit(0, view, entry);
            const bool visible = read_bit(slot.result_offset, view, entry);
            if (slot.consumed_previous && read_bit(slot.culled_offset, view, entry)) {
                ++counts.culled;
            }
            if (tested) {
                ++counts.tested;
                if (!visible) {
                    ++counts.occluded;
                }
            } else if (visible) {
                ++counts.untested;
            }
        }
        m_retired_occlusion_counts.push_back(counts);
    }
    m_retired_occlusion_frame_index = slot.frame_index;
    m_has_retired_occlusion_counts  = true;

    {
        // The check of this frame: per view its counts, the near plane
        // entries' bits and the first occluded entries.
        const std::lock_guard<std::mutex> lock{m_occlusion_check->mutex};
        Gpu_occlusion_check& check = *m_occlusion_check;
        if ((check.stage == Gpu_occlusion_check::Stage::recording) && (check.frame_index == slot.frame_index)) {
            constexpr std::size_t max_reported_entries = 16;
            ERHE_VERIFY(check.entry_count == entry_count);
            check.result = Occlusion_check{
                .frame_index = check.frame_index,
                .entry_count = check.entry_count,
                .view_count  = check.view_count
            };
            check.first_occluded_objects.clear();
            for (const Gpu_occlusion_check::View& check_view : check.views) {
                const std::size_t view = static_cast<std::size_t>(check_view.cull_view);
                Occlusion_view_check view_check{
                    .view               = static_cast<uint32_t>(view),
                    .key                = check_view.key,
                    .bounded_in_frustum = check_view.bounded_in_frustum,
                    .near_plane         = static_cast<uint32_t>(check_view.near_plane_entries.size())
                };
                std::vector<uint32_t>& occluded_objects = check.first_occluded_objects.emplace_back();
                for (std::size_t entry = 0; entry < entry_count; ++entry) {
                    const bool tested  = read_bit(0, view, entry);
                    const bool visible = read_bit(slot.result_offset, view, entry);
                    if (slot.consumed_previous && read_bit(slot.culled_offset, view, entry)) {
                        ++view_check.culled;
                    }
                    if (!tested) {
                        if (visible) {
                            ++view_check.untested;
                        }
                        continue;
                    }
                    ++view_check.tested;
                    if (visible) {
                        ++view_check.visible;
                        continue;
                    }
                    ++view_check.occluded;
                    if (view_check.first_occluded.size() < max_reported_entries) {
                        // The entry's list: the last list starting at or before it.
                        const std::vector<uint32_t>::const_iterator begin = check.list_entry_offsets.cbegin();
                        const std::vector<uint32_t>::const_iterator it    = std::upper_bound(begin, check.list_entry_offsets.cend(), static_cast<uint32_t>(entry));
                        ERHE_VERIFY(it != begin);
                        const std::size_t list_index = static_cast<std::size_t>(std::distance(begin, it)) - 1;
                        view_check.first_occluded.push_back(Occlusion_check_entry{
                            .list_index  = static_cast<uint32_t>(list_index),
                            .entry_index = static_cast<uint32_t>(entry) - check.list_entry_offsets[list_index]
                        });
                        occluded_objects.push_back(check.entry_objects[entry]);
                    }
                }
                for (const uint32_t entry : check_view.near_plane_entries) {
                    if (!read_bit(slot.result_offset, view, entry)) {
                        ++view_check.near_plane_occluded;
                    }
                }
                check.result.views.push_back(std::move(view_check));
            }
            check.stage = Gpu_occlusion_check::Stage::complete;
        }
    }
    slot.buffer->unmap();
    slot.pending = false;
}

void Draw_list_scene::request_occlusion_check()
{
    assert_main_thread();
    // A new request replaces any earlier one, whatever its stage.
    const std::lock_guard<std::mutex> lock{m_occlusion_check->mutex};
    Gpu_occlusion_check& check = *m_occlusion_check;
    check.stage = Gpu_occlusion_check::Stage::requested;
    check.views.clear();
    check.entry_objects.clear();
    check.result = Occlusion_check{};
    check.first_occluded_objects.clear();
}

auto Draw_list_scene::poll_occlusion_check(Occlusion_check& out) -> Cull_classification_check_state
{
    assert_main_thread();
    const std::lock_guard<std::mutex> lock{m_occlusion_check->mutex};
    Gpu_occlusion_check& check = *m_occlusion_check;
    switch (check.stage) {
        case Gpu_occlusion_check::Stage::idle:      return Cull_classification_check_state::idle;
        case Gpu_occlusion_check::Stage::requested: return Cull_classification_check_state::pending;
        case Gpu_occlusion_check::Stage::recording: return Cull_classification_check_state::pending;
        case Gpu_occlusion_check::Stage::complete:  break;
        default:                                    return Cull_classification_check_state::idle;
    }
    out = std::move(check.result);
    // The occluded entries' names, from the objects as they are now (main
    // thread state; an object unregistered since keeps no name).
    ERHE_VERIFY(check.first_occluded_objects.size() == out.views.size());
    for (std::size_t view = 0, end = out.views.size(); view < end; ++view) {
        Occlusion_view_check&        view_check = out.views[view];
        const std::vector<uint32_t>& objects    = check.first_occluded_objects[view];
        ERHE_VERIFY(objects.size() == view_check.first_occluded.size());
        for (std::size_t i = 0, i_end = objects.size(); i < i_end; ++i) {
            const uint32_t object_index = objects[i];
            if ((object_index < m_objects.size()) && m_objects[object_index].alive && m_objects[object_index].info.mesh) {
                view_check.first_occluded[i].name = m_objects[object_index].info.mesh->get_name();
            }
        }
    }
    check.stage = Gpu_occlusion_check::Stage::idle;
    check.views.clear();
    check.entry_objects.clear();
    check.result = Occlusion_check{};
    check.first_occluded_objects.clear();
    return Cull_classification_check_state::complete;
}

} // namespace erhe::scene_renderer
