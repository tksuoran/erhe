#include "erhe_graphics/indirect_count_draws.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_GRAPHICS_API_METAL4)
# include "erhe_graphics/metal4/metal4_indirect_count_draws.hpp"
#elif defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_indirect_count_draws.hpp"
#else
namespace erhe::graphics {

// Vulkan, OpenGL and the null device draw from the parameters the slots
// record (the native count draw); they keep no backend state.
class Indirect_count_draws_impl
{
public:
    Indirect_count_draws_impl(Device&, const erhe::utility::Debug_label&) {}
};

} // namespace erhe::graphics
#endif

namespace erhe::graphics {

Indirect_count_draws::Indirect_count_draws(Device& device, erhe::utility::Debug_label debug_label)
    : m_device     {device}
    , m_debug_label{std::move(debug_label)}
    , m_slots      (device.get_number_of_frames_in_flight())
    , m_impl       {std::make_unique<Indirect_count_draws_impl>(device, m_debug_label)}
{
}

Indirect_count_draws::~Indirect_count_draws() noexcept = default;

auto Indirect_count_draws::store(const Indirect_count_draws_prepare_info& prepare_info) -> Indirect_count_draw_set
{
    ERHE_VERIFY(!m_slots.empty());
    ERHE_VERIFY(prepare_info.command_buffer != nullptr);
    ERHE_VERIFY(prepare_info.count_buffer != nullptr);
    ERHE_VERIFY((prepare_info.command_byte_offset % sizeof(uint32_t)) == 0);
    ERHE_VERIFY((prepare_info.count_byte_offset % sizeof(uint32_t)) == 0);
    ERHE_VERIFY((prepare_info.count_byte_stride % sizeof(uint32_t)) == 0);
    ERHE_VERIFY(prepare_info.count_byte_stride >= sizeof(uint32_t));

    const std::size_t slot_index = static_cast<std::size_t>(prepare_info.frame_index % static_cast<uint64_t>(m_slots.size()));
    Slot& slot = m_slots[slot_index];
    // Frames are prepared in order; a frame may prepare any number of sets,
    // each replacing the previous one.
    ERHE_VERIFY(prepare_info.frame_index >= m_frame_index);
    m_frame_index = prepare_info.frame_index;
    ++m_sequence;

    slot.frame_index         = prepare_info.frame_index;
    slot.sequence            = m_sequence;
    slot.primitive_type      = prepare_info.primitive_type;
    slot.index_buffers.assign(prepare_info.index_buffers.begin(), prepare_info.index_buffers.end());
    slot.command_buffer      = prepare_info.command_buffer;
    slot.command_byte_offset = prepare_info.command_byte_offset;
    slot.count_buffer        = prepare_info.count_buffer;
    slot.count_byte_offset   = prepare_info.count_byte_offset;
    slot.count_byte_stride   = prepare_info.count_byte_stride;
    slot.jobs.assign(prepare_info.jobs.begin(), prepare_info.jobs.end());
    for (const Indirect_count_draw_job& job : slot.jobs) {
        ERHE_VERIFY(job.index_buffer < slot.index_buffers.size());
    }
    for (const Indirect_count_index_buffer& index_buffer : slot.index_buffers) {
        ERHE_VERIFY(index_buffer.buffer != nullptr);
    }
    return Indirect_count_draw_set{.draws = this, .slot = slot_index, .sequence = m_sequence};
}

auto Indirect_count_draws::get_slot(const std::size_t slot) const -> const Slot&
{
    ERHE_VERIFY(slot < m_slots.size());
    return m_slots[slot];
}

auto Indirect_count_draws::get_sequence() const -> uint64_t
{
    return m_sequence;
}

auto Indirect_count_draws::get_device() const -> Device&
{
    return m_device;
}

auto Indirect_count_draws::get_debug_label() const -> const erhe::utility::Debug_label&
{
    return m_debug_label;
}

auto Indirect_count_draws::get_impl() -> Indirect_count_draws_impl&
{
    return *m_impl;
}

auto Indirect_count_draws::get_impl() const -> const Indirect_count_draws_impl&
{
    return *m_impl;
}

} // namespace erhe::graphics
