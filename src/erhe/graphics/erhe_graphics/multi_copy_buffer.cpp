#include "erhe_graphics/multi_copy_buffer.hpp"

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::graphics {

namespace {

[[nodiscard]] auto align_up(const std::size_t value, const std::size_t alignment) -> std::size_t
{
    ERHE_VERIFY(alignment > 0);
    return ((value + alignment - 1) / alignment) * alignment;
}

}

Multi_copy_buffer::Multi_copy_buffer(Device& device, const Multi_copy_buffer_create_info& create_info)
    : m_device       {device}
    , m_buffer_target{create_info.buffer_target}
    , m_binding_point{create_info.binding_point}
    , m_debug_label  {create_info.debug_label}
{
    const std::size_t copy_count = (create_info.copy_count != 0)
        ? create_info.copy_count
        : device.get_number_of_frames_in_flight() + 1;
    ERHE_VERIFY(copy_count >= 2);  // one current + one to write into
    m_copies.resize(copy_count);
    const std::size_t frames_in_flight = device.get_number_of_frames_in_flight();
    ERHE_VERIFY(frames_in_flight >= 1);
    m_frame_slots.resize(frames_in_flight);
    m_alignment = std::max<std::size_t>(1, device.get_buffer_alignment(m_buffer_target));
    reallocate(std::max<std::size_t>(m_alignment, create_info.initial_copy_byte_count));
}

Multi_copy_buffer::~Multi_copy_buffer() noexcept = default;

void Multi_copy_buffer::reallocate(const std::size_t copy_byte_count)
{
    const std::size_t aligned_copy_byte_count = align_up(std::max<std::size_t>(1, copy_byte_count), m_alignment);
    if (m_buffer) {
        // The old allocation stays alive until every frame one of its copies
        // was published for has been retired.
        uint64_t last_used_frame = 0;
        for (const Copy& copy : m_copies) {
            if (copy.used) {
                last_used_frame = std::max(last_used_frame, copy.last_used_frame);
            }
        }
        m_retired.emplace_back(std::move(m_buffer), last_used_frame);
    }
    m_buffer = std::make_unique<Buffer>(
        m_device,
        Buffer_create_info{
            .capacity_byte_count                    = aligned_copy_byte_count * m_copies.size(),
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::none,
            .usage                                  = get_buffer_usage(m_buffer_target) | Buffer_usage::transfer,
            .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::device_local | Memory_property_flag_bit_mask::host_persistent,
            .debug_label                            = m_debug_label
        }
    );
    m_copy_byte_count = aligned_copy_byte_count;
    for (std::size_t i = 0, end = m_copies.size(); i < end; ++i) {
        // A fresh allocation shares no memory with the retired one, so every
        // copy in it is free regardless of what the old copies were doing.
        m_copies[i] = Copy{.byte_offset = i * aligned_copy_byte_count};
    }
    m_current = invalid_index;
    m_writing = invalid_index;
    ++m_allocation_count;
}

void Multi_copy_buffer::release_retired()
{
    if (m_retired.empty()) {
        return;
    }
    const auto is_completed = [this](const std::pair<std::unique_ptr<Buffer>, uint64_t>& entry) {
        return (entry.second == 0) || m_device.is_frame_completed(entry.second);
    };
    const auto i = std::remove_if(m_retired.begin(), m_retired.end(), is_completed);
    m_retired.erase(i, m_retired.end());
}

auto Multi_copy_buffer::find_free_copy() const -> std::size_t
{
    for (std::size_t i = 0, end = m_copies.size(); i < end; ++i) {
        if (i == m_current) {
            continue;
        }
        const Copy& copy = m_copies[i];
        if (!copy.used || m_device.is_frame_completed(copy.last_used_frame)) {
            return i;
        }
    }
    return invalid_index;
}

auto Multi_copy_buffer::begin_write(const std::size_t byte_count) -> std::span<std::byte>
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(byte_count > 0);

    release_retired();

    if (byte_count > m_copy_byte_count) {
        reallocate(byte_count);
    }
    std::size_t index = find_free_copy();
    if (index == invalid_index) {
        // Every other copy is still being read by an unretired frame. Rather
        // than stall the caller or overwrite a copy in flight, take a fresh
        // allocation; the old one is retired and freed when its frames land.
        reallocate(m_copy_byte_count);
        index = find_free_copy();
        ERHE_VERIFY(index != invalid_index);
    }
    m_writing       = index;
    m_writing_bytes = byte_count;
    const Copy& copy = m_copies[index];

    // Write through the persistent map where there is one, and through a CPU
    // staging block otherwise. Buffer::begin_write() is not usable here: on the
    // persistently mapped path it ignores its byte_offset and hands back the
    // whole-buffer map, so every copy would be written at offset zero. The
    // ring buffers take the same two paths for the same reason.
    const std::span<std::byte> map = m_buffer->get_map();
    if (map.size() >= (copy.byte_offset + byte_count)) {
        m_staging_in_use = false;
        return map.subspan(copy.byte_offset, byte_count);
    }
    m_staging_in_use = true;
    m_staging.resize(byte_count);
    return std::span<std::byte>{m_staging};
}

void Multi_copy_buffer::commit(const std::size_t byte_count)
{
    ERHE_VERIFY(m_writing != invalid_index);
    ERHE_VERIFY(byte_count <= m_copy_byte_count);
    ERHE_VERIFY(byte_count <= m_writing_bytes);
    Copy& copy = m_copies[m_writing];
    if (m_staging_in_use) {
        m_buffer->upload_sub_data(copy.byte_offset, byte_count, m_staging.data());
    } else {
        // No-op where the mapping is coherent or needs no explicit flush.
        m_buffer->flush_bytes(copy.byte_offset, byte_count);
    }
    copy.byte_count = byte_count;
    copy.written    = true;
    m_current       = m_writing;
    m_writing       = invalid_index;
    ++m_commit_count;
    publish();
}

void Multi_copy_buffer::publish()
{
    const uint64_t frame = m_device.get_frame_index();
    Frame_slot& slot = m_frame_slots[static_cast<std::size_t>(frame % m_frame_slots.size())];
    slot.frame     = frame;
    slot.published = true;
    if ((m_current == invalid_index) || (m_copies[m_current].byte_count == 0)) {
        slot.buffer      = nullptr;
        slot.byte_offset = 0;
        slot.byte_count  = 0;
        return;
    }
    // Stamped for every frame the copy is published for, not only for the
    // frame that committed it: the copy is being read by this frame too.
    Copy& copy = m_copies[m_current];
    copy.last_used_frame = frame;
    copy.used            = true;
    slot.buffer      = m_buffer.get();
    slot.byte_offset = copy.byte_offset;
    slot.byte_count  = copy.byte_count;
}

auto Multi_copy_buffer::bind(Command_encoder& command_encoder) -> bool
{
    ERHE_PROFILE_FUNCTION();
    // Only the published entry of the caller's own frame: the copy table and
    // the allocation belong to the thread that prepares the frames.
    const uint64_t    frame = m_device.get_frame_index();
    const Frame_slot& slot  = m_frame_slots[static_cast<std::size_t>(frame % m_frame_slots.size())];
    ERHE_VERIFY(slot.published && (slot.frame == frame));
    if (slot.buffer == nullptr) {
        return false;
    }
    ERHE_VERIFY(slot.byte_offset + slot.byte_count <= slot.buffer->get_capacity_byte_count());
    if (m_binding_point.has_value()) {
        ERHE_VERIFY(is_indexed(m_buffer_target));
        command_encoder.set_buffer(m_buffer_target, slot.buffer, slot.byte_offset, slot.byte_count, m_binding_point.value());
    } else {
        ERHE_VERIFY(!is_indexed(m_buffer_target));
        command_encoder.set_buffer(m_buffer_target, slot.buffer);
    }
    return true;
}

auto Multi_copy_buffer::has_current() const -> bool
{
    return (m_current != invalid_index) && m_copies[m_current].written;
}

auto Multi_copy_buffer::get_current_byte_count() const -> std::size_t
{
    return (m_current != invalid_index) ? m_copies[m_current].byte_count : 0;
}

auto Multi_copy_buffer::get_current_byte_offset() const -> std::size_t
{
    return (m_current != invalid_index) ? m_copies[m_current].byte_offset : 0;
}

auto Multi_copy_buffer::get_copy_byte_count () const -> std::size_t { return m_copy_byte_count; }
auto Multi_copy_buffer::get_copy_count      () const -> std::size_t { return m_copies.size(); }
auto Multi_copy_buffer::get_commit_count    () const -> std::size_t { return m_commit_count; }
auto Multi_copy_buffer::get_allocation_count() const -> std::size_t { return m_allocation_count; }
auto Multi_copy_buffer::get_retired_count   () const -> std::size_t { return m_retired.size(); }

} // namespace erhe::graphics
