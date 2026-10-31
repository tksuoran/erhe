#include "erhe_scene_renderer/pvs_gpu_database.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_scene/pvs_database.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::scene_renderer {

Pvs_gpu_database::Pvs_gpu_database(
    erhe::graphics::Device* const                    graphics_device,
    std::shared_ptr<const erhe::scene::Pvs_database> database,
    const uint64_t                                   generation,
    const uint32_t                                   capacity
)
    : m_database  {std::move(database)}
    , m_generation{generation}
    , m_capacity  {capacity}
{
    using namespace erhe::graphics;

    ERHE_VERIFY(m_database);
    ERHE_VERIFY(m_capacity >= 1);
    m_words_per_page = m_database->get_words_per_page();
    m_slot_words.assign(static_cast<std::size_t>(m_capacity) * m_words_per_page, 0xffffffffu);
    m_slot_pages.assign(m_capacity, no_page);
    m_byte_count = m_slot_words.size() * sizeof(uint32_t);
    if (graphics_device != nullptr) {
        m_buffer = std::make_unique<Buffer>(
            *graphics_device,
            Buffer_create_info{
                .capacity_byte_count               = std::max(m_byte_count, std::size_t{256}),
                .usage                             = Buffer_usage::storage | Buffer_usage::transfer_dst,
                .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
                .debug_label                       = erhe::utility::Debug_label{"Pvs_gpu_database slots"}
            }
        );
    }
    log_draw_list->info(
        "PVS resident slots: {} slots of {} words ({} bytes) for {} pages ({} stored), generation {}",
        m_capacity, m_words_per_page, m_byte_count, m_database->get_page_count(), m_database->get_stored_page_count(), m_generation
    );
}

Pvs_gpu_database::~Pvs_gpu_database() noexcept = default;

auto Pvs_gpu_database::get_buffer() const -> const erhe::graphics::Buffer*
{
    return m_buffer.get();
}

auto Pvs_gpu_database::get_slot_page(const uint32_t slot) const -> uint32_t
{
    return (slot < m_slot_pages.size()) ? m_slot_pages[slot] : no_page;
}

void Pvs_gpu_database::write_page(
    erhe::graphics::Command_buffer& command_buffer,
    const uint32_t                  slot,
    const uint32_t                  page,
    const std::span<const uint32_t> words
)
{
    ERHE_VERIFY(slot < m_capacity);
    ERHE_VERIFY(words.size() == m_words_per_page);
    const std::size_t first_word = static_cast<std::size_t>(slot) * m_words_per_page;
    std::copy(words.begin(), words.end(), m_slot_words.begin() + static_cast<std::ptrdiff_t>(first_word));
    m_slot_pages[slot] = page;
    ++m_write_count;
    if (!m_buffer) {
        return;
    }
    // No frame that may still be in flight reads the slot (the residency
    // writes an evicted slot only frames_in_flight frames after the frame
    // whose page table dropped its page), so the upload, a host write on a
    // host-visible buffer, races nothing; it orders its own output for the
    // classification that reads it (doc/erhe/graphics.md "Resource
    // barriers").
    command_buffer.upload_to_buffer(*m_buffer, first_word * sizeof(uint32_t), words.data(), words.size_bytes());
}

} // namespace erhe::scene_renderer
