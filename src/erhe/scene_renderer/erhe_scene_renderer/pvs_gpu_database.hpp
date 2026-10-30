#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace erhe::graphics {
    class Buffer;
    class Command_buffer;
    class Device;
}
namespace erhe::scene {
    class Pvs_database;
}

namespace erhe::scene_renderer {

// The GPU copy of a potentially visible set database (doc/plans/pvs.md
// "Residency and streaming"), the consumer side of erhe::scene::Pvs_database
// that res/shaders/cull_classify.comp reads: one device-local buffer of
// uint32 words, the GPU page table (per page of the database's page grid its
// slot, or no_slot) followed by the slots (resident pages, words_per_page
// words each, the page's cells in local cell order). A page without a slot
// reads as everything visible.
//
// Residency is trivial: every stored page is resident, in the order of the
// page table (capacity = the stored page count). Built and uploaded once
// per database (Command_buffer::upload_to_buffer()); the buffer is released
// with the object (the backends defer the destruction past the frames in
// flight). Owned by Draw_list_scene (render thread).
class Pvs_gpu_database
{
public:
    static constexpr uint32_t no_slot = 0xffffffffu;

    // Records the upload into command_buffer.
    Pvs_gpu_database(
        erhe::graphics::Device&                          graphics_device,
        erhe::graphics::Command_buffer&                  command_buffer,
        std::shared_ptr<const erhe::scene::Pvs_database> database
    );
    ~Pvs_gpu_database() noexcept;
    Pvs_gpu_database(const Pvs_gpu_database&)            = delete;
    Pvs_gpu_database& operator=(const Pvs_gpu_database&) = delete;

    // The database this copy was made of (held: a new database at the same
    // address is never mistaken for it).
    [[nodiscard]] auto get_database        () const -> const erhe::scene::Pvs_database* { return m_database.get(); }
    [[nodiscard]] auto get_buffer          () const -> const erhe::graphics::Buffer*;
    [[nodiscard]] auto get_byte_count      () const -> std::size_t { return m_byte_count; }
    // The page table's words (the database's page count): the first slot
    // word.
    [[nodiscard]] auto get_slot_base_word  () const -> uint32_t { return m_slot_base_word; }
    [[nodiscard]] auto get_resident_page_count() const -> uint32_t { return m_resident_page_count; }

private:
    std::shared_ptr<const erhe::scene::Pvs_database> m_database;
    std::unique_ptr<erhe::graphics::Buffer>          m_buffer;
    std::size_t                                      m_byte_count         {0};
    uint32_t                                         m_slot_base_word     {0};
    uint32_t                                         m_resident_page_count{0};
};

} // namespace erhe::scene_renderer
