#include "erhe_scene_renderer/pvs_residency.hpp"

#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::scene_renderer {

auto Pvs_streaming_settings::clamped() const -> Pvs_streaming_settings
{
    return Pvs_streaming_settings{
        .resident_pages      = std::clamp(resident_pages, min_resident_pages, max_resident_pages),
        .stream_radius_pages = std::min(stream_radius_pages, max_stream_radius_pages)
    };
}

Pvs_residency::Pvs_residency(const glm::uvec3& page_dims, const std::span<const uint8_t> stored_pages, const Pvs_streaming_settings& settings)
    : m_page_dims{page_dims}
    , m_radius   {settings.stream_radius_pages}
{
    const std::size_t page_count = static_cast<std::size_t>(page_dims.x) * page_dims.y * page_dims.z;
    ERHE_VERIFY(stored_pages.size() == page_count);
    ERHE_VERIFY(settings == settings.clamped());
    m_stored.assign(stored_pages.begin(), stored_pages.end());
    m_in_flight.assign(page_count, uint8_t{0});
    m_failed.assign(page_count, uint8_t{0});
    m_page_slots.assign(page_count, no_slot);
    m_slot_pages.assign(settings.resident_pages, no_page);
    m_slot_usable_frame.assign(settings.resident_pages, uint64_t{0});
    m_slot_reserved.assign(settings.resident_pages, uint8_t{0});
}

auto Pvs_residency::page_coordinate(const uint32_t page) const -> glm::uvec3
{
    return glm::uvec3{
        page % m_page_dims.x,
        (page / m_page_dims.x) % m_page_dims.y,
        page / (m_page_dims.x * m_page_dims.y)
    };
}

auto Pvs_residency::page_index(const glm::uvec3& page) const -> uint32_t
{
    return page.x + (m_page_dims.x * (page.y + (m_page_dims.y * page.z)));
}

auto Pvs_residency::box_distance(const glm::uvec3& page, const Pvs_page_box& box) -> uint32_t
{
    uint32_t distance = 0;
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        uint32_t axis_distance = 0;
        if (page[axis] < box.min[axis]) {
            axis_distance = box.min[axis] - page[axis];
        } else if (page[axis] > box.max[axis]) {
            axis_distance = page[axis] - box.max[axis];
        }
        distance = std::max(distance, axis_distance);
    }
    return distance;
}

auto Pvs_residency::is_active(const Pvs_residency_view& view, const uint64_t frame_index) const -> bool
{
    return (view.last_frame + 1) >= frame_index;
}

auto Pvs_residency::view_distance(const uint32_t page, const uint64_t frame_index) const -> uint32_t
{
    const glm::uvec3 coordinate = page_coordinate(page);
    uint32_t distance = infinite_distance;
    for (const Pvs_residency_view& view : m_views) {
        if (is_active(view, frame_index)) {
            distance = std::min(distance, box_distance(coordinate, view.pages));
        }
    }
    return distance;
}

auto Pvs_residency::is_stored(const uint32_t page) const -> bool
{
    return (page < m_stored.size()) && (m_stored[page] != 0);
}

auto Pvs_residency::is_in_flight(const uint32_t page) const -> bool
{
    return (page < m_in_flight.size()) && (m_in_flight[page] != 0);
}

auto Pvs_residency::get_slot(const uint32_t page) const -> uint32_t
{
    return (page < m_page_slots.size()) ? m_page_slots[page] : no_slot;
}

auto Pvs_residency::set_view(const uint64_t view_key, const Pvs_page_box& pages, const uint64_t frame_index) -> bool
{
    ERHE_VERIFY((pages.max.x < m_page_dims.x) && (pages.max.y < m_page_dims.y) && (pages.max.z < m_page_dims.z));
    ERHE_VERIFY((pages.min.x <= pages.max.x) && (pages.min.y <= pages.max.y) && (pages.min.z <= pages.max.z));
    Pvs_residency_view* found = nullptr;
    for (Pvs_residency_view& view : m_views) {
        if (view.view_key == view_key) {
            found = &view;
            break;
        }
    }
    bool changed = false;
    if (found == nullptr) {
        // An inactive view's entry is reused (the views stay as many as
        // are registered at once).
        for (Pvs_residency_view& view : m_views) {
            if (!is_active(view, frame_index)) {
                found = &view;
                break;
            }
        }
        if (found == nullptr) {
            m_views.push_back(Pvs_residency_view{});
            found = &m_views.back();
        }
        found->view_key = view_key;
        changed = true;
    } else {
        changed = !is_active(*found, frame_index) || !(found->pages == pages);
    }
    // Called by every view registration, every frame: the registration is
    // the change site doc/plans/pvs.md "Residency and streaming" names (the
    // query box is registered each frame anyway, AGENTS.md "No update each
    // frame patterns"), and the compare against the view's cached page box
    // above is how a change is detected; only a change rebuilds anything.
    found->pages      = pages;
    found->last_frame = frame_index;
    if (changed) {
        rebuild_requests(frame_index);
    }
    return changed;
}

void Pvs_residency::rebuild_requests(const uint64_t frame_index)
{
    m_requests.clear();
    m_request_cursor = 0;
    for (const Pvs_residency_view& view : m_views) {
        if (!is_active(view, frame_index)) {
            continue;
        }
        const glm::uvec3 low{
            (view.pages.min.x > m_radius) ? (view.pages.min.x - m_radius) : 0u,
            (view.pages.min.y > m_radius) ? (view.pages.min.y - m_radius) : 0u,
            (view.pages.min.z > m_radius) ? (view.pages.min.z - m_radius) : 0u
        };
        const glm::uvec3 high{
            std::min(view.pages.max.x + m_radius, m_page_dims.x - 1),
            std::min(view.pages.max.y + m_radius, m_page_dims.y - 1),
            std::min(view.pages.max.z + m_radius, m_page_dims.z - 1)
        };
        for (uint32_t z = low.z; z <= high.z; ++z) {
            for (uint32_t y = low.y; y <= high.y; ++y) {
                for (uint32_t x = low.x; x <= high.x; ++x) {
                    const uint32_t page = page_index(glm::uvec3{x, y, z});
                    if (
                        (m_stored[page] == 0) ||
                        (m_page_slots[page] != no_slot) ||
                        (m_in_flight[page] != 0) ||
                        (m_failed[page] != 0)
                    ) {
                        continue;
                    }
                    // In flight marks nothing here; a page two views want is
                    // listed twice and deduplicated below.
                    m_requests.push_back(Request{.distance = view_distance(page, frame_index), .page = page});
                }
            }
        }
    }
    std::sort(
        m_requests.begin(), m_requests.end(),
        [](const Request& lhs, const Request& rhs) -> bool {
            return (lhs.distance != rhs.distance) ? (lhs.distance < rhs.distance) : (lhs.page < rhs.page);
        }
    );
    m_requests.erase(
        std::unique(
            m_requests.begin(), m_requests.end(),
            [](const Request& lhs, const Request& rhs) -> bool { return lhs.page == rhs.page; }
        ),
        m_requests.end()
    );
}

auto Pvs_residency::find_free_slot() const -> uint32_t
{
    // A slot is free only until its first placement: an eviction hands the
    // slot to the arriving page at once.
    for (uint32_t slot = 0, end = static_cast<uint32_t>(m_slot_pages.size()); slot < end; ++slot) {
        if (m_slot_pages[slot] == no_page) {
            return slot;
        }
    }
    return no_slot;
}

auto Pvs_residency::find_victim_slot(const uint32_t request_distance, const uint64_t frame_index) const -> uint32_t
{
    if (request_distance == infinite_distance) {
        return no_slot;
    }
    uint32_t victim          = no_slot;
    uint32_t victim_distance = 0;
    for (uint32_t slot = 0, end = static_cast<uint32_t>(m_slot_pages.size()); slot < end; ++slot) {
        const uint32_t page = m_slot_pages[slot];
        if ((page == no_page) || (m_slot_reserved[slot] != 0)) {
            continue;
        }
        const uint32_t distance = view_distance(page, frame_index);
        // Beyond the hysteresis: more than one page farther than the request.
        if (static_cast<uint64_t>(distance) <= (static_cast<uint64_t>(request_distance) + 1u)) {
            continue;
        }
        if ((victim == no_slot) || (distance > victim_distance)) {
            victim          = slot;
            victim_distance = distance;
        }
    }
    return victim;
}

auto Pvs_residency::take_request(const uint64_t frame_index) -> uint32_t
{
    while (m_request_cursor < m_requests.size()) {
        const uint32_t page = m_requests[m_request_cursor].page;
        if ((m_page_slots[page] != no_slot) || (m_in_flight[page] != 0) || (m_failed[page] != 0)) {
            ++m_request_cursor;
            continue;
        }
        if ((find_free_slot() == no_slot) && (find_victim_slot(view_distance(page, frame_index), frame_index) == no_slot)) {
            // Every later request is at least as far away: none can be
            // placed until a view moves.
            m_request_cursor = m_requests.size();
            return no_page;
        }
        ++m_request_cursor;
        m_in_flight[page] = 1;
        return page;
    }
    return no_page;
}

auto Pvs_residency::place(const uint32_t page, const uint64_t frame_index, const uint32_t reuse_delay_frames) -> Pvs_placement
{
    ERHE_VERIFY(is_in_flight(page));
    ERHE_VERIFY(m_page_slots[page] == no_slot);
    Pvs_placement placement{};
    uint32_t slot = find_free_slot();
    if (slot == no_slot) {
        slot = find_victim_slot(view_distance(page, frame_index), frame_index);
        if (slot == no_slot) {
            m_in_flight[page] = 0;
            return placement;
        }
        // The evicted page leaves the page table of this frame; frames
        // before it may still read the slot.
        const uint32_t evicted = m_slot_pages[slot];
        m_page_slots[evicted]  = no_slot;
        m_slot_pages[slot]     = no_page;
        m_slot_usable_frame[slot] = frame_index + reuse_delay_frames;
        --m_resident_count;
        ++m_serial;
        placement.evicted_page = evicted;
    }
    m_slot_pages[slot]    = page;
    m_slot_reserved[slot] = 1;
    placement.slot         = slot;
    placement.usable_frame = std::max(m_slot_usable_frame[slot], frame_index);
    return placement;
}

void Pvs_residency::commit(const uint32_t page, const uint64_t frame_index)
{
    ERHE_VERIFY(is_in_flight(page));
    ERHE_VERIFY(m_page_slots[page] == no_slot);
    uint32_t slot = no_slot;
    for (uint32_t i = 0, end = static_cast<uint32_t>(m_slot_pages.size()); i < end; ++i) {
        if ((m_slot_pages[i] == page) && (m_slot_reserved[i] != 0)) {
            slot = i;
            break;
        }
    }
    ERHE_VERIFY(slot != no_slot);
    // The delayed reuse: no frame that may still read the slot's previous
    // page is in flight.
    ERHE_VERIFY(frame_index >= m_slot_usable_frame[slot]);
    m_in_flight[page]     = 0;
    m_slot_reserved[slot] = 0;
    m_page_slots[page]    = slot;
    ++m_resident_count;
    ++m_serial;
}

void Pvs_residency::fail(const uint32_t page)
{
    ERHE_VERIFY(is_in_flight(page));
    m_in_flight[page] = 0;
    m_failed[page]    = 1;
    ++m_failed_count;
}

void Pvs_residency::count_view_pages(const Pvs_page_box& pages, uint32_t& wanted, uint32_t& resident) const
{
    wanted   = 0;
    resident = 0;
    const glm::uvec3 low{
        (pages.min.x > m_radius) ? (pages.min.x - m_radius) : 0u,
        (pages.min.y > m_radius) ? (pages.min.y - m_radius) : 0u,
        (pages.min.z > m_radius) ? (pages.min.z - m_radius) : 0u
    };
    const glm::uvec3 high{
        std::min(pages.max.x + m_radius, m_page_dims.x - 1),
        std::min(pages.max.y + m_radius, m_page_dims.y - 1),
        std::min(pages.max.z + m_radius, m_page_dims.z - 1)
    };
    for (uint32_t z = low.z; z <= high.z; ++z) {
        for (uint32_t y = low.y; y <= high.y; ++y) {
            for (uint32_t x = low.x; x <= high.x; ++x) {
                const uint32_t page = page_index(glm::uvec3{x, y, z});
                if (m_stored[page] == 0) {
                    continue;
                }
                ++wanted;
                if (m_page_slots[page] != no_slot) {
                    ++resident;
                }
            }
        }
    }
}

auto Pvs_residency::is_box_resident(const Pvs_page_box& pages) const -> bool
{
    for (uint32_t z = pages.min.z; z <= pages.max.z; ++z) {
        for (uint32_t y = pages.min.y; y <= pages.max.y; ++y) {
            for (uint32_t x = pages.min.x; x <= pages.max.x; ++x) {
                const uint32_t page = page_index(glm::uvec3{x, y, z});
                if ((m_stored[page] != 0) && (m_page_slots[page] == no_slot)) {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace erhe::scene_renderer
