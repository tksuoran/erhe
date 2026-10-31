#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Command_buffer;
    class Device;
}
namespace erhe::scene {
    class Pvs_database;
}

namespace erhe::scene_renderer {

// The resident page slots of a potentially visible set database
// (doc/plans/pvs.md "Residency and streaming"), the consumer side of
// erhe::scene::Pvs_database: one device-local buffer of capacity slots
// (words_per_page uint32 words each, a page's cells in local cell order)
// that res/shaders/cull_classify.comp reads, and its CPU copy, which the
// CPU lanes read (cull_lanes::make_pvs_query_cells()). Which page a slot
// holds in a frame is that frame's page table (Pvs_residency, copied per
// frame slot by Draw_list_scene::publish()); a page without a slot reads as
// everything visible.
//
// Made for one residency generation (a database with its streaming
// settings) and written page by page: write_page() records the upload of
// one page into one slot that no page table of a frame that may still be in
// flight names. Render thread only (Draw_list_scene); the buffer
// is released with the object (the backends defer the destruction past the
// frames in flight).
class Pvs_gpu_database
{
public:
    static constexpr uint32_t no_slot = 0xffffffffu;
    static constexpr uint32_t no_page = 0xffffffffu;

    // graphics_device null: the CPU copy only (a device without the GPU
    // classification).
    Pvs_gpu_database(
        erhe::graphics::Device*                          graphics_device,
        std::shared_ptr<const erhe::scene::Pvs_database> database,
        uint64_t                                         generation,
        uint32_t                                         capacity
    );
    ~Pvs_gpu_database() noexcept;
    Pvs_gpu_database(const Pvs_gpu_database&)            = delete;
    Pvs_gpu_database& operator=(const Pvs_gpu_database&) = delete;

    // Writes words (words_per_page) into slot as page: the CPU copy at
    // once, the GPU slot (when there is one) by an upload recorded into
    // command_buffer. No page table of a frame that may still be in flight
    // names the slot (Pvs_residency delays an evicted slot's reuse by the
    // frames in flight): the write races no reader.
    void write_page(erhe::graphics::Command_buffer& command_buffer, uint32_t slot, uint32_t page, std::span<const uint32_t> words);

    // The database this copy was made of (held: a new database at the same
    // address is never mistaken for it).
    [[nodiscard]] auto get_database        () const -> const erhe::scene::Pvs_database* { return m_database.get(); }
    [[nodiscard]] auto get_generation      () const -> uint64_t { return m_generation; }
    [[nodiscard]] auto get_capacity        () const -> uint32_t { return m_capacity; }
    [[nodiscard]] auto get_words_per_page  () const -> uint32_t { return m_words_per_page; }
    [[nodiscard]] auto get_buffer          () const -> const erhe::graphics::Buffer*;
    [[nodiscard]] auto get_byte_count      () const -> std::size_t { return m_byte_count; }
    // The CPU copy of the slots (capacity * words_per_page words).
    [[nodiscard]] auto get_slot_words      () const -> std::span<const uint32_t> { return m_slot_words; }
    // The page the slot holds (no_page: never written).
    [[nodiscard]] auto get_slot_page       (uint32_t slot) const -> uint32_t;
    // Pages written (uploads applied) since creation.
    [[nodiscard]] auto get_write_count     () const -> uint64_t { return m_write_count; }

private:
    std::shared_ptr<const erhe::scene::Pvs_database> m_database;
    uint64_t                                         m_generation    {0};
    uint32_t                                         m_capacity      {0};
    uint32_t                                         m_words_per_page{0};
    std::unique_ptr<erhe::graphics::Buffer>          m_buffer;
    std::size_t                                      m_byte_count    {0};
    std::vector<uint32_t>                            m_slot_words;
    std::vector<uint32_t>                            m_slot_pages;
    uint64_t                                         m_write_count   {0};
};

} // namespace erhe::scene_renderer
