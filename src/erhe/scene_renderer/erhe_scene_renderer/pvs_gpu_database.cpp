#include "erhe_scene_renderer/pvs_gpu_database.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_scene/pvs_database.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace erhe::scene_renderer {

Pvs_gpu_database::Pvs_gpu_database(
    erhe::graphics::Device&                          graphics_device,
    erhe::graphics::Command_buffer&                  command_buffer,
    std::shared_ptr<const erhe::scene::Pvs_database> database
)
    : m_database{std::move(database)}
{
    using namespace erhe::graphics;

    ERHE_VERIFY(m_database);
    const erhe::scene::Pvs_database& source = *m_database;
    const uint32_t page_count     = source.get_page_count();
    const uint32_t words_per_page = source.get_words_per_page();
    const uint32_t stored_pages   = source.get_stored_page_count();
    m_slot_base_word      = page_count;
    m_resident_page_count = stored_pages;

    // A one-shot staging copy, made once per database (set or baked).
    std::vector<uint32_t> words(static_cast<std::size_t>(page_count) + (static_cast<std::size_t>(stored_pages) * words_per_page), no_slot);
    uint32_t slot = 0;
    for (uint32_t page = 0; page < page_count; ++page) {
        const std::span<const uint32_t> page_words = source.get_page_words(page);
        if (page_words.empty()) {
            continue;
        }
        ERHE_VERIFY(page_words.size() == words_per_page);
        ERHE_VERIFY(slot < stored_pages);
        words[page] = slot;
        std::copy(page_words.begin(), page_words.end(), words.begin() + m_slot_base_word + (static_cast<std::ptrdiff_t>(slot) * words_per_page));
        ++slot;
    }
    ERHE_VERIFY(slot == stored_pages);

    m_byte_count = words.size() * sizeof(uint32_t);
    m_buffer = std::make_unique<Buffer>(
        graphics_device,
        Buffer_create_info{
            .capacity_byte_count               = std::max(m_byte_count, std::size_t{256}),
            .usage                             = Buffer_usage::storage | Buffer_usage::transfer_dst,
            .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
            .debug_label                       = erhe::utility::Debug_label{"Pvs_gpu_database"}
        }
    );
    command_buffer.upload_to_buffer(*m_buffer, 0, words.data(), m_byte_count);
    log_draw_list->info(
        "PVS GPU database: {} pages ({} resident, {} words each), {} bytes",
        page_count, stored_pages, words_per_page, m_byte_count
    );
}

Pvs_gpu_database::~Pvs_gpu_database() noexcept = default;

auto Pvs_gpu_database::get_buffer() const -> const erhe::graphics::Buffer*
{
    return m_buffer.get();
}

} // namespace erhe::scene_renderer
