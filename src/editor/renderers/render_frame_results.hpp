#pragma once

#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace editor {

// What the render phase (R) produces for the main thread to read: one T per
// device frame in flight, written by R into the slot of the frame it renders
// (Device::get_frame_index() on the render role) and read by the main thread
// (M2, M3, MCP) as the result of the last rendered frame, the newest frame
// the main thread joined (App_context::last_rendered_frame_index,
// doc/editor/editor.md "Frame phases and the render thread").
//
// The two sides never touch the same slot at once: R(N) writes slot
// N % frames_in_flight while the main thread reads the slot of a frame it
// joined, at most N - 1 (with frame pipelining, M2 / M3 of frame N + 1 run
// while R(N) renders and the newest joined frame is N - 1; without it the
// main thread reads N after the join, while R is idle). A slot is stamped
// with the frame it holds; find() returns null for a frame R did not write
// a result for (the owner did not render in that frame, or nothing was
// rendered yet), so a reader never sees an older frame's value as the last
// rendered frame's.
//
// T is reused in place: write() hands back the slot with whatever the frame
// frames_in_flight earlier left in it, so the owner overwrites what it
// publishes and keeps the capacity of its containers.
template <typename T>
class Render_frame_results
{
public:
    void resize(const std::size_t frames_in_flight)
    {
        ERHE_VERIFY(frames_in_flight > 0);
        m_slots.resize(frames_in_flight);
    }

    [[nodiscard]] auto is_empty() const -> bool
    {
        return m_slots.empty();
    }

    // Render phase: the slot of frame_index, stamped with it. Calling it
    // again in the same frame returns the same slot.
    [[nodiscard]] auto write(const uint64_t frame_index) -> T&
    {
        erhe::utility::Thread_roles::assert_render_thread();
        Slot& slot = get_slot(frame_index);
        slot.frame_index = frame_index;
        return slot.value;
    }

    // Render phase: the slot R already wrote for frame_index (verified).
    [[nodiscard]] auto get(const uint64_t frame_index) const -> const T&
    {
        const Slot& slot = get_slot(frame_index);
        ERHE_VERIFY(slot.frame_index == frame_index);
        return slot.value;
    }

    // The result of frame_index, or null when R wrote none for that frame
    // or frame_index is empty (no frame rendered yet). The main thread
    // passes the last rendered frame index.
    [[nodiscard]] auto find(const std::optional<uint64_t> frame_index) const -> const T*
    {
        if (!frame_index.has_value() || m_slots.empty()) {
            return nullptr;
        }
        const Slot& slot = get_slot(frame_index.value());
        return (slot.frame_index == frame_index.value()) ? &slot.value : nullptr;
    }

private:
    class Slot
    {
    public:
        uint64_t frame_index{std::numeric_limits<uint64_t>::max()};
        T        value{};
    };

    [[nodiscard]] auto get_slot(const uint64_t frame_index) -> Slot&
    {
        ERHE_VERIFY(!m_slots.empty());
        return m_slots[static_cast<std::size_t>(frame_index % static_cast<uint64_t>(m_slots.size()))];
    }
    [[nodiscard]] auto get_slot(const uint64_t frame_index) const -> const Slot&
    {
        ERHE_VERIFY(!m_slots.empty());
        return m_slots[static_cast<std::size_t>(frame_index % static_cast<uint64_t>(m_slots.size()))];
    }

    std::vector<Slot> m_slots;
};

} // namespace editor
