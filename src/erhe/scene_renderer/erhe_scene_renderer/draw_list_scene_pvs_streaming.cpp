// The PVS page streaming of Draw_list_scene (doc/plans/pvs.md "Residency and
// streaming"; doc/erhe/draw_list_renderer.md Q6 "PVS").

#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/gpu_cull_classifier.hpp"
#include "erhe_scene_renderer/pvs_gpu_database.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_scene/pvs_database.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace erhe::scene_renderer {

namespace {

// Host flush granularity: a multiple of every nonCoherentAtomSize.
constexpr std::size_t c_pvs_page_table_alignment = 256;

} // anonymous namespace

void Draw_list_scene::set_pvs_streaming_settings(const Pvs_streaming_settings& settings)
{
    assert_main_thread();
    const Pvs_streaming_settings clamped = settings.clamped();
    if (clamped == m_pvs_streaming_settings) {
        return;
    }
    m_pvs_streaming_settings = clamped;
    rebuild_pvs_residency();
}

void Draw_list_scene::rebuild_pvs_residency()
{
    assert_main_thread();
    // A new generation: reads, uploads and frame slots of the previous one
    // are dropped where they arrive (the worker's results, the render
    // phase's upload queue, the render phase's resident slots).
    ++m_pvs_residency_generation;
    m_pvs_read_page         = Pvs_residency::no_page;
    m_pvs_placement_pending = false;
    if (m_pvs_page_streamer) {
        m_pvs_page_streamer->cancel();
    }
    m_pvs_residency.reset();
    m_pvs_uploads               = 0;
    m_pvs_evictions             = 0;
    m_pvs_last_frame_uploads    = 0;
    m_pvs_last_frame_evictions  = 0;
    m_pvs_max_uploads_per_frame = 0;
    m_pvs_max_resident_pages    = 0;
    if (!m_pvs_database) {
        return;
    }
    const erhe::scene::Pvs_database& database = *m_pvs_database;
    std::vector<uint8_t> stored(database.get_page_count(), uint8_t{0});
    for (uint32_t page = 0, end = database.get_page_count(); page < end; ++page) {
        stored[page] = database.is_page_stored(page) ? uint8_t{1} : uint8_t{0};
    }
    m_pvs_residency = std::make_unique<Pvs_residency>(database.get_page_dims(), std::span<const uint8_t>{stored}, m_pvs_streaming_settings);
    log_draw_list->info(
        "PVS residency generation {}: {} resident page slots, stream radius {} pages, {} pages ({} stored, {} words each)",
        m_pvs_residency_generation, m_pvs_streaming_settings.resident_pages, m_pvs_streaming_settings.stream_radius_pages,
        database.get_page_count(), database.get_stored_page_count(), database.get_words_per_page()
    );
}

void Draw_list_scene::register_pvs_view(const uint64_t view_key, const uint64_t frame_index, const glm::vec3& query_min, const glm::vec3& query_max)
{
    assert_main_thread();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    if (!m_pvs_residency || !m_pvs_database || (m_pvs_database->get_state() != erhe::scene::Pvs_state::ready)) {
        return;
    }
    const erhe::scene::Pvs_header& header            = m_pvs_database->get_header();
    const float                    inverse_cell_size = cull_lanes::get_pvs_inverse_cell_size(*m_pvs_database);
    // The corner's cell, as the classification computes it, clamped to the
    // grid: a box outside the grid streams the pages at its edge.
    const auto page_of = [&header, inverse_cell_size](const glm::vec3& point) -> glm::uvec3 {
        glm::uvec3 page{0u};
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            const float offset = point[axis] - header.grid_origin[axis];
            const float scaled = offset * inverse_cell_size;
            const float cell   = std::floor(scaled);
            const float last   = static_cast<float>(header.cell_counts[axis] - 1);
            const float clamped = (cell >= 0.0f) ? std::min(cell, last) : 0.0f; // NaN -> 0
            page[axis] = static_cast<uint32_t>(clamped) / header.page_size;
        }
        return page;
    };
    const Pvs_page_box pages{.min = page_of(query_min), .max = page_of(query_max)};
    static_cast<void>(m_pvs_residency->set_view(view_key, pages, frame_index));
}

void Draw_list_scene::publish_pvs_residency(Draw_list_slot& slot, const uint64_t frame_index, const std::size_t frames_in_flight)
{
    ERHE_PROFILE_FUNCTION();
    m_pvs_last_frame_index     = frame_index;
    m_pvs_last_frame_uploads   = 0;
    m_pvs_last_frame_evictions = 0;
    if (!m_pvs_residency) {
        slot.pvs_generation = 0;
        slot.pvs_residency_database.reset();
        slot.pvs_capacity = 0;
        return;
    }
    Pvs_residency& residency  = *m_pvs_residency;
    const uint64_t generation = m_pvs_residency_generation;

    // A finished read: its slot reserved (evicting a page, whose slot is
    // written again only once no frame in flight can read it,
    // frames_in_flight frames later).
    if ((m_pvs_read_page != Pvs_residency::no_page) && !m_pvs_placement_pending) {
        while (m_pvs_page_streamer && m_pvs_page_streamer->take_result(m_pvs_read)) {
            if (m_pvs_read.generation != generation) {
                continue; // a read of a previous residency
            }
            ERHE_VERIFY(m_pvs_read.page == m_pvs_read_page);
            if (!m_pvs_read.ok) {
                log_draw_list->warn("PVS: page {} could not be read; it stays non-resident (everything visible)", m_pvs_read.page);
                residency.fail(m_pvs_read.page);
                m_pvs_read_page = Pvs_residency::no_page;
                break;
            }
            m_pvs_placement = residency.place(m_pvs_read.page, frame_index, static_cast<uint32_t>(frames_in_flight));
            if (m_pvs_placement.slot == Pvs_residency::no_slot) {
                m_pvs_read_page = Pvs_residency::no_page; // no slot: dropped until a view moves
                break;
            }
            m_pvs_placement_pending = true;
            if (m_pvs_placement.evicted_page != Pvs_residency::no_page) {
                ++m_pvs_evictions;
                m_pvs_last_frame_evictions = 1;
            }
            break;
        }
    }
    // The placed page's upload, in the first frame that may write its slot:
    // at most one per frame. The page table of this frame and the later
    // ones names the slot; the render phase of this frame (or of the next
    // one that classifies) writes it before it classifies.
    if (m_pvs_placement_pending && (frame_index >= m_pvs_placement.usable_frame) && !m_pvs_upload_queue.is_full()) {
        const bool pushed = m_pvs_upload_queue.push(frame_index, generation, m_pvs_read.page, m_pvs_placement.slot, std::span<const uint32_t>{m_pvs_read.words});
        ERHE_VERIFY(pushed);
        residency.commit(m_pvs_read.page, frame_index);
        m_pvs_placement_pending = false;
        m_pvs_read_page         = Pvs_residency::no_page;
        ++m_pvs_uploads;
        m_pvs_last_frame_uploads = 1;
    }
    // The next read.
    if (m_pvs_read_page == Pvs_residency::no_page) {
        const uint32_t page = residency.take_request(frame_index);
        if (page != Pvs_residency::no_page) {
            if (!m_pvs_page_streamer) {
                m_pvs_page_streamer = std::make_unique<Pvs_page_streamer>(m_executor);
            }
            m_pvs_page_streamer->request(m_pvs_database, generation, page);
            m_pvs_read_page = page;
        }
    }
    ERHE_VERIFY(residency.get_resident_count() <= residency.get_capacity());
    m_pvs_max_resident_pages    = std::max(m_pvs_max_resident_pages, residency.get_resident_count());
    m_pvs_max_uploads_per_frame = std::max(m_pvs_max_uploads_per_frame, m_pvs_last_frame_uploads);

    // The frame's page table: copied when it changed since this slot's copy.
    const std::span<const uint32_t> table = residency.get_page_table();
    if (
        (slot.pvs_generation != generation) ||
        (slot.pvs_table_serial != residency.get_serial()) ||
        (slot.pvs_page_table.size() != table.size())
    ) {
        slot.pvs_page_table.assign(table.begin(), table.end());
        slot.pvs_table_serial = residency.get_serial();
    }
    slot.pvs_generation         = generation;
    slot.pvs_residency_database = m_pvs_database;
    slot.pvs_capacity           = residency.get_capacity();
}

void Draw_list_scene::publish_gpu_pvs_page_table(Draw_list_slot& slot)
{
    if (slot.pvs_generation == 0) {
        return;
    }
    if ((slot.gpu_pvs_table_generation == slot.pvs_generation) && (slot.gpu_pvs_table_serial == slot.pvs_table_serial)) {
        return;
    }
    ERHE_VERIFY(m_gpu_cull_classifier != nullptr);
    erhe::graphics::Device& graphics_device = m_gpu_cull_classifier->get_device();
    const std::size_t byte_count = std::max(slot.pvs_page_table.size() * sizeof(uint32_t), std::size_t{4});
    // The slot's previous frame retired before its prepare phase (as for
    // gpu_bounds): the write never races the GPU.
    if (!slot.gpu_pvs_page_table || (slot.gpu_pvs_page_table_capacity < byte_count)) {
        const std::size_t capacity = ((byte_count + c_pvs_page_table_alignment - 1) / c_pvs_page_table_alignment) * c_pvs_page_table_alignment;
        slot.gpu_pvs_page_table = std::make_unique<erhe::graphics::Buffer>(
            graphics_device,
            erhe::graphics::Buffer_create_info{
                .capacity_byte_count                    = capacity,
                .memory_allocation_create_flag_bit_mask = erhe::graphics::Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = erhe::graphics::Buffer_usage::storage,
                .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::host_coherent | erhe::graphics::Memory_property_flag_bit_mask::host_persistent,
                .debug_label                            = erhe::utility::Debug_label{"Draw_list_scene GPU PVS page table"}
            }
        );
        slot.gpu_pvs_page_table_capacity = capacity;
    }
    const std::span<std::byte> mapped = slot.gpu_pvs_page_table->map_bytes(0, slot.gpu_pvs_page_table_capacity);
    std::memcpy(mapped.data(), slot.pvs_page_table.data(), slot.pvs_page_table.size() * sizeof(uint32_t));
    slot.gpu_pvs_page_table->flush_bytes(0, slot.gpu_pvs_page_table_capacity);
    slot.gpu_pvs_page_table->unmap();
    slot.gpu_pvs_table_generation = slot.pvs_generation;
    slot.gpu_pvs_table_serial     = slot.pvs_table_serial;
}

void Draw_list_scene::apply_pvs_uploads(const uint64_t frame_index, erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();
    assert_render_phase();
    const Draw_list_slot& slot = get_published_slot(frame_index);
    if ((slot.pvs_generation != 0) && (!m_pvs_gpu_database || (m_pvs_gpu_database->get_generation() != slot.pvs_generation))) {
        ERHE_VERIFY(slot.pvs_residency_database);
        erhe::graphics::Device* const device = ((m_gpu_cull_classifier != nullptr) && m_gpu_cull_classifier->is_supported())
            ? &m_gpu_cull_classifier->get_device()
            : nullptr;
        m_pvs_gpu_database = std::make_unique<Pvs_gpu_database>(device, slot.pvs_residency_database, slot.pvs_generation, slot.pvs_capacity);
        const std::lock_guard<std::mutex> lock{m_pvs_render_counters_mutex};
        m_pvs_render_generation  = slot.pvs_generation;
        m_pvs_applied_uploads    = 0;
        m_pvs_nonresident_views  = 0;
        m_pvs_nonresident_culled = 0;
        m_pvs_nonresident_logged = false;
    } else if ((slot.pvs_generation == 0) && m_pvs_gpu_database) {
        m_pvs_gpu_database.reset();
    }
    uint64_t applied = 0;
    m_pvs_upload_queue.consume(
        frame_index,
        [this, &command_buffer, &applied](const Pvs_page_upload& upload) {
            if (!m_pvs_gpu_database || (upload.generation != m_pvs_gpu_database->get_generation())) {
                return; // an upload of a previous residency
            }
            m_pvs_gpu_database->write_page(command_buffer, upload.slot, upload.page, std::span<const uint32_t>{upload.words});
            ++applied;
        }
    );
    if (applied > 0) {
        const std::lock_guard<std::mutex> lock{m_pvs_render_counters_mutex};
        m_pvs_applied_uploads += applied;
    }
}

auto Draw_list_scene::get_pvs_resident_pages(const Draw_list_slot& slot) const -> cull_lanes::Pvs_resident_pages
{
    if ((slot.pvs_generation == 0) || !m_pvs_gpu_database) {
        return cull_lanes::Pvs_resident_pages{};
    }
    // apply_pvs_uploads() of this frame made the slots of its generation.
    ERHE_VERIFY(m_pvs_gpu_database->get_generation() == slot.pvs_generation);
    return cull_lanes::Pvs_resident_pages{
        .page_table     = std::span<const uint32_t>{slot.pvs_page_table},
        .slot_words     = m_pvs_gpu_database->get_slot_words(),
        .words_per_page = m_pvs_gpu_database->get_words_per_page()
    };
}

auto Draw_list_scene::check_pvs_query_cells(const cull_lanes::Pvs_query_cells& cells) const -> bool
{
    if (cells.keep_all) {
        return true;
    }
    if (!m_pvs_gpu_database) {
        return false;
    }
    for (uint32_t i = 0; i < cells.cell_count; ++i) {
        if (m_pvs_gpu_database->get_slot_page(cells.slots[i]) != cells.pages[i]) {
            return false;
        }
    }
    return true;
}

void Draw_list_scene::get_pvs_streaming_status(Pvs_streaming_status& out) const
{
    assert_main_thread();
    // Reset with the views' capacity kept (a window may call this per frame).
    std::vector<Pvs_streaming_view_status> views = std::move(out.views);
    views.clear();
    out = Pvs_streaming_status{};
    out.views = std::move(views);
    out.settings   = m_pvs_streaming_settings;
    out.generation = m_pvs_residency_generation;
    if (m_pvs_database) {
        out.page_count     = m_pvs_database->get_page_count();
        out.stored_pages   = m_pvs_database->get_stored_page_count();
        out.words_per_page = m_pvs_database->get_words_per_page();
        out.bits_in_memory = m_pvs_database->has_resident_bits();
    }
    if (!m_pvs_residency) {
        return;
    }
    const Pvs_residency& residency = *m_pvs_residency;
    out.active                = true;
    out.capacity              = residency.get_capacity();
    out.resident_pages        = residency.get_resident_count();
    out.max_resident_pages    = m_pvs_max_resident_pages;
    out.slot_bytes            = static_cast<std::size_t>(out.capacity) * out.words_per_page * sizeof(uint32_t);
    out.uploads               = m_pvs_uploads;
    out.evictions             = m_pvs_evictions;
    out.last_frame_index      = m_pvs_last_frame_index;
    out.last_frame_uploads    = m_pvs_last_frame_uploads;
    out.last_frame_evictions  = m_pvs_last_frame_evictions;
    out.max_uploads_per_frame = m_pvs_max_uploads_per_frame;
    out.pending_requests      = residency.get_pending_count();
    out.read_in_flight        = (m_pvs_read_page != Pvs_residency::no_page);
    out.placement_pending     = m_pvs_placement_pending;
    out.queued_uploads        = m_pvs_upload_queue.get_size();
    out.failed_pages          = residency.get_failed_count();
    {
        const std::lock_guard<std::mutex> lock{m_pvs_render_counters_mutex};
        if (m_pvs_render_generation == m_pvs_residency_generation) {
            out.applied_uploads    = m_pvs_applied_uploads;
            out.nonresident_views  = m_pvs_nonresident_views;
            out.nonresident_culled = m_pvs_nonresident_culled;
        }
    }
    for (const Pvs_residency_view& view : residency.get_views()) {
        Pvs_streaming_view_status view_status{};
        view_status.view_key   = view.view_key;
        view_status.last_frame = view.last_frame;
        view_status.pages      = view.pages;
        view_status.active     = (view.last_frame + 1) >= m_pvs_last_frame_index;
        residency.count_view_pages(view.pages, view_status.wanted_pages, view_status.resident_pages);
        view_status.box_resident = residency.is_box_resident(view.pages);
        out.views.push_back(view_status);
    }
}

} // namespace erhe::scene_renderer
