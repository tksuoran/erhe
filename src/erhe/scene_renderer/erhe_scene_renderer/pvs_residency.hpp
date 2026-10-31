#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace erhe::scene_renderer {

// The settings of the potentially visible set page streaming
// (doc/plans/pvs.md "Residency and streaming"): the number of page slots the
// GPU holds and the radius, in pages, around a view's page that its
// registration requests.
class Pvs_streaming_settings
{
public:
    // The ranges every source of the settings is clamped to (the editor
    // settings, the Settings window, MCP): the radius bound keeps the page
    // box arithmetic far from overflow.
    static constexpr uint32_t min_resident_pages     = 1;
    static constexpr uint32_t max_resident_pages     = 65536;
    static constexpr uint32_t max_stream_radius_pages = 64;

    uint32_t resident_pages     {64};
    uint32_t stream_radius_pages{1};

    // These settings clamped to the ranges above.
    [[nodiscard]] auto clamped() const -> Pvs_streaming_settings;
    [[nodiscard]] auto operator==(const Pvs_streaming_settings& other) const -> bool = default;
};

// An inclusive box of page coordinates: the pages a view's query box
// touches (one page unless the box straddles a page boundary).
class Pvs_page_box
{
public:
    glm::uvec3 min{0u, 0u, 0u};
    glm::uvec3 max{0u, 0u, 0u};

    [[nodiscard]] auto operator==(const Pvs_page_box& other) const -> bool = default;
};

// Where a page that arrived goes (Pvs_residency::place()).
class Pvs_placement
{
public:
    // The slot reserved for the page, or Pvs_residency::no_slot when it
    // could not be placed (no free slot, nothing evictable).
    uint32_t slot        {0xffffffffu};
    // The page whose slot it took, or Pvs_residency::no_page.
    uint32_t evicted_page{0xffffffffu};
    // The first frame whose render phase may write the slot (no frame that
    // may still be in flight reads it): the frame to commit() the page in.
    uint64_t usable_frame{0};
};

// One view of the residency (Pvs_residency::get_views()).
class Pvs_residency_view
{
public:
    uint64_t     view_key  {0};
    uint64_t     last_frame{0};
    Pvs_page_box pages     {};
};

// The residency policy of the PVS page streaming (doc/plans/pvs.md
// "Residency and streaming"): which stored pages of a database hold one of
// the capacity page slots, plain bookkeeping with no GPU objects and no
// threads (the owner, Draw_list_scene, feeds it on the main thread in the
// prepare phase).
//
// - The views register their page box (set_view(), at the query-box
//   registration). When the box differs from the view's previous one (or the
//   view was inactive), the request list is rebuilt: the stored pages within
//   stream_radius_pages (Chebyshev distance in pages from the box) of any
//   active view, neither resident, in flight nor failed, nearest first
//   (distance, then page index). A view is active in frame F when it
//   registered in frame F or F - 1.
// - take_request() hands out the front request when a slot can be had for
//   it; place() reserves a slot for an arrived page: a never used one, else the slot of the resident page farthest from every
//   active view (largest distance, lowest slot on ties) when that page is
//   more than one page (the hysteresis) farther away than the arriving
//   page. A page within one page of the arriving page's distance is never
//   evicted for it, so a view moving back and forth across a page boundary
//   does not thrash. An evicted page leaves the page table at once, but its
//   slot is written again only reuse_delay_frames later (the frames that
//   may still read it have retired): commit() makes the page resident in
//   the frame its placement names.
// - Not stored pages never get a slot (they read as everything visible).
class Pvs_residency
{
public:
    static constexpr uint32_t no_slot           = 0xffffffffu;
    static constexpr uint32_t no_page           = 0xffffffffu;
    static constexpr uint32_t infinite_distance = 0xffffffffu;

    // stored_pages[page] != 0 for a stored page (page_dims.x * y * z
    // entries). capacity >= 1.
    Pvs_residency(const glm::uvec3& page_dims, std::span<const uint8_t> stored_pages, const Pvs_streaming_settings& settings);

    // The change site (a view's query box registration in frame_index):
    // records the view's page box and rebuilds the request list when it
    // changed or the view was inactive. Returns true when it rebuilt.
    auto set_view(uint64_t view_key, const Pvs_page_box& pages, uint64_t frame_index) -> bool;

    // The front request if a slot can be had for it now (a free slot or an
    // evictable page); marks it in flight. no_page when there is none; a
    // front request that cannot be placed clears the list (every later one
    // is at least as far away).
    [[nodiscard]] auto take_request(uint64_t frame_index) -> uint32_t;
    // An in-flight page's words arrived in frame_index: reserves its slot,
    // evicting as above; an evicted slot is usable reuse_delay_frames after
    // frame_index. The page stays in flight until commit().
    [[nodiscard]] auto place(uint32_t page, uint64_t frame_index, uint32_t reuse_delay_frames) -> Pvs_placement;
    // The placed page becomes resident in frame_index: its slot enters the
    // page table. frame_index must be the placement's usable_frame or later
    // (verified: the policy enforces the delayed reuse).
    void commit(uint32_t page, uint64_t frame_index);
    // An in-flight page could not be read: it is never requested again.
    void fail(uint32_t page);

    // Per page its slot (no_slot when not resident): the page table.
    [[nodiscard]] auto get_page_table   () const -> std::span<const uint32_t> { return m_page_slots; }
    // Per slot its page (no_page when free; a reserved slot names the page
    // placed in it before its commit()).
    [[nodiscard]] auto get_slot_pages   () const -> std::span<const uint32_t> { return m_slot_pages; }
    [[nodiscard]] auto get_slot         (uint32_t page) const -> uint32_t;
    // Moves with every page table change.
    [[nodiscard]] auto get_serial       () const -> uint64_t { return m_serial; }
    [[nodiscard]] auto get_capacity     () const -> uint32_t { return static_cast<uint32_t>(m_slot_pages.size()); }
    [[nodiscard]] auto get_radius       () const -> uint32_t { return m_radius; }
    [[nodiscard]] auto get_page_dims    () const -> glm::uvec3 { return m_page_dims; }
    [[nodiscard]] auto get_resident_count() const -> uint32_t { return m_resident_count; }
    [[nodiscard]] auto get_pending_count() const -> std::size_t { return m_requests.size() - m_request_cursor; }
    [[nodiscard]] auto get_failed_count () const -> uint32_t { return m_failed_count; }
    [[nodiscard]] auto get_views        () const -> std::span<const Pvs_residency_view> { return m_views; }
    [[nodiscard]] auto is_stored        (uint32_t page) const -> bool;
    [[nodiscard]] auto is_in_flight     (uint32_t page) const -> bool;

    // Chebyshev distance in pages from a page to a page box.
    [[nodiscard]] static auto box_distance(const glm::uvec3& page, const Pvs_page_box& box) -> uint32_t;
    // The distance of a page to the nearest active view (infinite_distance
    // without one).
    [[nodiscard]] auto view_distance(uint32_t page, uint64_t frame_index) const -> uint32_t;
    // The stored pages within the radius of a view's box and how many of
    // them are resident.
    void count_view_pages(const Pvs_page_box& pages, uint32_t& wanted, uint32_t& resident) const;
    // True when every stored page the box touches is resident.
    [[nodiscard]] auto is_box_resident(const Pvs_page_box& pages) const -> bool;
    [[nodiscard]] auto page_coordinate(uint32_t page) const -> glm::uvec3;
    [[nodiscard]] auto page_index     (const glm::uvec3& page) const -> uint32_t;

private:
    [[nodiscard]] auto is_active(const Pvs_residency_view& view, uint64_t frame_index) const -> bool;
    void rebuild_requests(uint64_t frame_index);
    // The lowest never-used slot, or no_slot.
    [[nodiscard]] auto find_free_slot() const -> uint32_t;
    // The slot of the page an arriving page at distance request_distance
    // may evict, or no_slot.
    [[nodiscard]] auto find_victim_slot(uint32_t request_distance, uint64_t frame_index) const -> uint32_t;

    class Request
    {
    public:
        uint32_t distance{0};
        uint32_t page    {0};
    };

    glm::uvec3                      m_page_dims{0u, 0u, 0u};
    uint32_t                        m_radius   {1};
    std::vector<uint8_t>            m_stored;
    std::vector<uint8_t>            m_in_flight;
    std::vector<uint8_t>            m_failed;
    std::vector<uint32_t>           m_page_slots;
    std::vector<uint32_t>           m_slot_pages;
    // Per slot: the first frame a write into it is allowed (a slot freed by
    // an eviction waits for the frames that may still read it).
    std::vector<uint64_t>           m_slot_usable_frame;
    // Per slot: reserved by place() for a page not committed yet.
    std::vector<uint8_t>            m_slot_reserved;
    uint32_t                        m_resident_count{0};
    uint32_t                        m_failed_count  {0};
    uint64_t                        m_serial        {1};
    std::vector<Pvs_residency_view> m_views;
    // Rebuilt at a change (capacity kept); m_request_cursor is the front.
    std::vector<Request>            m_requests;
    std::size_t                     m_request_cursor{0};
};

} // namespace erhe::scene_renderer
