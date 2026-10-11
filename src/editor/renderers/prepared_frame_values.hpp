#pragma once

#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace editor {

// What the prepare phase (M2) hands the render phase (R) of one frame: one T
// per device frame in flight, written by the main thread into the slot of
// the frame it prepares (Device::get_frame_index() on the main role) and
// read by the render phase of that frame (the role-keyed index on the render
// role), the counterpart of Render_frame_results (doc/editor/editor.md
// "Frame phases and the render thread").
//
// The owner keeps the live value the change sites edit (an ImGui slider, a
// tool, MCP) and copies it into the frame's slot in M2: a plain assignment
// of a small value, so a later prepare phase or the ImGui phase never
// writes what the render phase of an earlier frame reads. A slot is
// stamped with its frame; get() verifies it, find() returns null for a
// frame whose prepare phase did not fill the slot.
//
// T is reused in place: prepare() hands back the slot with whatever the
// frame frames_in_flight earlier left in it, so containers keep their
// capacity.
template <typename T>
class Prepared_frame_values
{
public:
    void resize(const std::size_t frames_in_flight)
    {
        ERHE_VERIFY(frames_in_flight > 0);
        m_slots.resize(frames_in_flight);
    }

    // Main thread, prepare phase: the slot of frame_index, stamped with it.
    [[nodiscard]] auto prepare(const uint64_t frame_index) -> T&
    {
        erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
        Slot& slot = get_slot(frame_index);
        slot.frame_index = frame_index;
        return slot.value;
    }

    // Render phase: the slot the prepare phase filled for frame_index
    // (verified).
    [[nodiscard]] auto get(const uint64_t frame_index) const -> const T&
    {
        const Slot& slot = get_slot(frame_index);
        ERHE_VERIFY(slot.frame_index == frame_index);
        return slot.value;
    }

    // The slot of frame_index, or null when its prepare phase filled none.
    [[nodiscard]] auto find(const uint64_t frame_index) const -> const T*
    {
        if (m_slots.empty()) {
            return nullptr;
        }
        const Slot& slot = get_slot(frame_index);
        return (slot.frame_index == frame_index) ? &slot.value : nullptr;
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
