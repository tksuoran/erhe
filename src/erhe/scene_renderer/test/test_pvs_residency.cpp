// The residency policy of the PVS page streaming (doc/plans/pvs.md
// "Residency and streaming"; erhe::scene_renderer::Pvs_residency): page
// selection around a view, the union of several views, the eviction order
// and the hysteresis.

#include "erhe_scene_renderer/pvs_residency.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

using erhe::scene_renderer::Pvs_page_box;
using erhe::scene_renderer::Pvs_placement;
using erhe::scene_renderer::Pvs_residency;
using erhe::scene_renderer::Pvs_streaming_settings;

[[nodiscard]] auto all_stored(const glm::uvec3& dims) -> std::vector<uint8_t>
{
    return std::vector<uint8_t>(static_cast<std::size_t>(dims.x) * dims.y * dims.z, uint8_t{1});
}

[[nodiscard]] auto box_at(const glm::uvec3& page) -> Pvs_page_box
{
    return Pvs_page_box{.min = page, .max = page};
}

class Served
{
public:
    std::vector<uint32_t> pages;   // in the order they were requested
    std::vector<uint32_t> evicted; // in eviction order
    uint32_t              dropped{0};
};

// Takes and places every request the residency hands out (a worker that
// reads instantly), within frame_index (the views stay active).
auto serve_all(Pvs_residency& residency, const uint64_t frame_index) -> Served
{
    Served served{};
    for (;;) {
        const uint32_t page = residency.take_request(frame_index);
        if (page == Pvs_residency::no_page) {
            break;
        }
        served.pages.push_back(page);
        const Pvs_placement placement = residency.place(page, frame_index, 0);
        if (placement.slot == Pvs_residency::no_slot) {
            ++served.dropped;
        } else {
            if (placement.evicted_page != Pvs_residency::no_page) {
                served.evicted.push_back(placement.evicted_page);
            }
            EXPECT_EQ(placement.usable_frame, frame_index);
            residency.commit(page, frame_index);
        }
        EXPECT_LE(residency.get_resident_count(), residency.get_capacity());
    }
    return served;
}

} // anonymous namespace

TEST(Pvs_residency, requests_the_pages_within_the_radius_nearest_first)
{
    const glm::uvec3            dims{5u, 1u, 5u};
    const std::vector<uint8_t>  stored = all_stored(dims);
    Pvs_residency residency{dims, stored, Pvs_streaming_settings{.resident_pages = 64, .stream_radius_pages = 1}};
    uint64_t frame_index = 1;
    EXPECT_TRUE(residency.set_view(1, box_at(glm::uvec3{2u, 0u, 2u}), frame_index));
    EXPECT_EQ(residency.get_pending_count(), 9u);
    const Served served = serve_all(residency, frame_index);
    ASSERT_EQ(served.pages.size(), 9u);
    // The view's own page first, then the eight around it by page index.
    EXPECT_EQ(served.pages.front(), residency.page_index(glm::uvec3{2u, 0u, 2u}));
    for (std::size_t i = 1; i < served.pages.size(); ++i) {
        EXPECT_EQ(Pvs_residency::box_distance(residency.page_coordinate(served.pages[i]), box_at(glm::uvec3{2u, 0u, 2u})), 1u);
        if (i > 1) {
            EXPECT_LT(served.pages[i - 1], served.pages[i]);
        }
    }
    EXPECT_TRUE(served.evicted.empty());
    EXPECT_EQ(residency.get_resident_count(), 9u);
    // The same box again is no change: nothing is requested.
    EXPECT_FALSE(residency.set_view(1, box_at(glm::uvec3{2u, 0u, 2u}), frame_index));
    EXPECT_EQ(residency.take_request(frame_index), Pvs_residency::no_page);
}

TEST(Pvs_residency, clips_at_the_grid_and_skips_pages_not_stored)
{
    const glm::uvec3     dims{5u, 1u, 5u};
    std::vector<uint8_t> stored = all_stored(dims);
    Pvs_residency probe{dims, stored, Pvs_streaming_settings{}};
    stored[probe.page_index(glm::uvec3{1u, 0u, 0u})] = 0u;
    Pvs_residency residency{dims, stored, Pvs_streaming_settings{.resident_pages = 64, .stream_radius_pages = 1}};
    uint64_t frame_index = 1;
    residency.set_view(1, box_at(glm::uvec3{0u, 0u, 0u}), frame_index);
    const Served served = serve_all(residency, frame_index);
    // (0,0), (0,1), (1,1): (1,0) is not stored.
    EXPECT_EQ(served.pages.size(), 3u);
    EXPECT_EQ(std::count(served.pages.begin(), served.pages.end(), residency.page_index(glm::uvec3{1u, 0u, 0u})), 0);
    uint32_t wanted   = 0;
    uint32_t resident = 0;
    residency.count_view_pages(box_at(glm::uvec3{0u, 0u, 0u}), wanted, resident);
    EXPECT_EQ(wanted, 3u);
    EXPECT_EQ(resident, 3u);
    EXPECT_TRUE(residency.is_box_resident(box_at(glm::uvec3{0u, 0u, 0u})));
    EXPECT_TRUE(residency.is_box_resident(box_at(glm::uvec3{1u, 0u, 0u}))); // not stored: nothing to be resident
    EXPECT_FALSE(residency.is_box_resident(box_at(glm::uvec3{3u, 0u, 3u})));
}

TEST(Pvs_residency, the_requests_of_several_views_union)
{
    const glm::uvec3           dims{5u, 1u, 5u};
    const std::vector<uint8_t> stored = all_stored(dims);
    {
        // Apart: 4 + 4 corner pages.
        Pvs_residency residency{dims, stored, Pvs_streaming_settings{.resident_pages = 64, .stream_radius_pages = 1}};
        uint64_t frame_index = 1;
        residency.set_view(1, box_at(glm::uvec3{0u, 0u, 0u}), frame_index);
        residency.set_view(2, box_at(glm::uvec3{4u, 0u, 4u}), frame_index);
        const Served served = serve_all(residency, frame_index);
        EXPECT_EQ(served.pages.size(), 8u);
    }
    {
        // Overlapping: 3 x 3 and 3 x 3 sharing 2 x 3 = 12 distinct pages,
        // each requested once.
        Pvs_residency residency{dims, stored, Pvs_streaming_settings{.resident_pages = 64, .stream_radius_pages = 1}};
        uint64_t frame_index = 1;
        residency.set_view(1, box_at(glm::uvec3{2u, 0u, 2u}), frame_index);
        residency.set_view(2, box_at(glm::uvec3{3u, 0u, 2u}), frame_index);
        const Served served = serve_all(residency, frame_index);
        EXPECT_EQ(served.pages.size(), 12u);
        std::vector<uint32_t> sorted = served.pages;
        std::sort(sorted.begin(), sorted.end());
        EXPECT_EQ(std::adjacent_find(sorted.begin(), sorted.end()), sorted.end());
        // Both views' own pages come first (distance 0).
        EXPECT_TRUE(
            ((served.pages[0] == residency.page_index(glm::uvec3{2u, 0u, 2u})) && (served.pages[1] == residency.page_index(glm::uvec3{3u, 0u, 2u}))) ||
            ((served.pages[1] == residency.page_index(glm::uvec3{2u, 0u, 2u})) && (served.pages[0] == residency.page_index(glm::uvec3{3u, 0u, 2u})))
        );
    }
    {
        // A box straddling a page boundary is distance 0 for both pages.
        Pvs_residency residency{dims, stored, Pvs_streaming_settings{.resident_pages = 64, .stream_radius_pages = 0}};
        uint64_t frame_index = 1;
        residency.set_view(1, Pvs_page_box{.min = glm::uvec3{1u, 0u, 1u}, .max = glm::uvec3{2u, 0u, 1u}}, frame_index);
        const Served served = serve_all(residency, frame_index);
        EXPECT_EQ(served.pages.size(), 2u);
    }
}

TEST(Pvs_residency, evicts_the_page_farthest_from_every_view)
{
    const glm::uvec3           dims{7u, 1u, 1u};
    const std::vector<uint8_t> stored = all_stored(dims);
    Pvs_residency residency{dims, stored, Pvs_streaming_settings{.resident_pages = 3, .stream_radius_pages = 1}};
    uint64_t frame_index = 1;
    residency.set_view(1, box_at(glm::uvec3{1u, 0u, 0u}), frame_index);
    Served served = serve_all(residency, frame_index);
    EXPECT_EQ(served.pages, (std::vector<uint32_t>{1u, 0u, 2u}));
    EXPECT_EQ(residency.get_resident_count(), 3u);

    // The view moves to x = 5: 5 (distance 0) evicts 0 (distance 5), 4
    // (distance 1) evicts 1 (distance 4), 6 (distance 1) evicts 2 (3).
    residency.set_view(1, box_at(glm::uvec3{5u, 0u, 0u}), frame_index);
    served = serve_all(residency, frame_index);
    EXPECT_EQ(served.pages,   (std::vector<uint32_t>{5u, 4u, 6u}));
    EXPECT_EQ(served.evicted, (std::vector<uint32_t>{0u, 1u, 2u}));
    EXPECT_EQ(served.dropped, 0u);
    EXPECT_EQ(residency.get_resident_count(), 3u);
    for (const uint32_t page : {4u, 5u, 6u}) {
        EXPECT_NE(residency.get_slot(page), Pvs_residency::no_slot);
    }
}

TEST(Pvs_residency, hysteresis_keeps_pages_within_one_page_of_the_request)
{
    const glm::uvec3           dims{7u, 1u, 1u};
    const std::vector<uint8_t> stored = all_stored(dims);
    Pvs_residency residency{dims, stored, Pvs_streaming_settings{.resident_pages = 3, .stream_radius_pages = 1}};
    uint64_t frame_index = 1;
    residency.set_view(1, box_at(glm::uvec3{2u, 0u, 0u}), frame_index);
    Served served = serve_all(residency, frame_index);
    EXPECT_EQ(residency.get_resident_count(), 3u); // 1, 2, 3

    // One page to x = 3: 4 (distance 1) could only evict 1 (distance 2,
    // not beyond the hysteresis): nothing is read or evicted.
    residency.set_view(1, box_at(glm::uvec3{3u, 0u, 0u}), frame_index);
    served = serve_all(residency, frame_index);
    EXPECT_TRUE(served.pages.empty());
    EXPECT_TRUE(served.evicted.empty());
    // And back: everything wanted is resident.
    residency.set_view(1, box_at(glm::uvec3{2u, 0u, 0u}), frame_index);
    served = serve_all(residency, frame_index);
    EXPECT_TRUE(served.pages.empty());
    // Back and forth across the boundary: no thrash.
    for (int i = 0; i < 4; ++i) {
        residency.set_view(1, box_at(glm::uvec3{(i % 2 == 0) ? 3u : 2u, 0u, 0u}), frame_index);
        served = serve_all(residency, frame_index);
        EXPECT_TRUE(served.evicted.empty());
    }

    // Two pages to x = 4: the view's own page 4 (distance 0) evicts 1
    // (distance 3); 5 (distance 1) cannot evict 2 (distance 2).
    residency.set_view(1, box_at(glm::uvec3{4u, 0u, 0u}), frame_index);
    served = serve_all(residency, frame_index);
    EXPECT_EQ(served.pages,   (std::vector<uint32_t>{4u}));
    EXPECT_EQ(served.evicted, (std::vector<uint32_t>{1u}));
    EXPECT_NE(residency.get_slot(4u), Pvs_residency::no_slot);
    EXPECT_EQ(residency.get_slot(5u), Pvs_residency::no_slot);
}

TEST(Pvs_residency, an_inactive_view_holds_no_pages)
{
    const glm::uvec3           dims{7u, 1u, 1u};
    const std::vector<uint8_t> stored = all_stored(dims);
    Pvs_residency residency{dims, stored, Pvs_streaming_settings{.resident_pages = 3, .stream_radius_pages = 1}};
    uint64_t frame_index = 1;
    residency.set_view(1, box_at(glm::uvec3{1u, 0u, 0u}), frame_index);
    static_cast<void>(serve_all(residency, frame_index));
    // View 1 stops registering; view 2 appears far away: view 1's pages are
    // evictable (they are as far as view 2 sees them).
    frame_index += 10;
    EXPECT_TRUE(residency.set_view(2, box_at(glm::uvec3{5u, 0u, 0u}), frame_index));
    const Served served = serve_all(residency, frame_index);
    EXPECT_EQ(served.pages.size(), 3u);
    EXPECT_EQ(served.evicted.size(), 3u);
    // View 1 registering its unchanged box again after being inactive is a
    // change (its pages may have left).
    EXPECT_TRUE(residency.set_view(1, box_at(glm::uvec3{1u, 0u, 0u}), frame_index));
}

TEST(Pvs_residency, a_failed_page_is_not_requested_again)
{
    const glm::uvec3           dims{3u, 1u, 1u};
    const std::vector<uint8_t> stored = all_stored(dims);
    Pvs_residency residency{dims, stored, Pvs_streaming_settings{.resident_pages = 4, .stream_radius_pages = 0}};
    uint64_t frame_index = 1;
    residency.set_view(1, box_at(glm::uvec3{1u, 0u, 0u}), frame_index);
    const uint32_t page = residency.take_request(frame_index);
    ASSERT_EQ(page, 1u);
    EXPECT_TRUE(residency.is_in_flight(page));
    residency.fail(page);
    EXPECT_EQ(residency.get_failed_count(), 1u);
    residency.set_view(1, box_at(glm::uvec3{0u, 0u, 0u}), frame_index);
    residency.set_view(1, box_at(glm::uvec3{1u, 0u, 0u}), frame_index);
    EXPECT_EQ(residency.take_request(frame_index), Pvs_residency::no_page);
}

TEST(Pvs_residency, an_evicted_slot_is_written_again_only_after_the_frames_in_flight)
{
    const glm::uvec3           dims{7u, 1u, 1u};
    const std::vector<uint8_t> stored = all_stored(dims);
    Pvs_residency residency{dims, stored, Pvs_streaming_settings{.resident_pages = 1, .stream_radius_pages = 0}};
    residency.set_view(1, box_at(glm::uvec3{0u, 0u, 0u}), 10);
    ASSERT_EQ(residency.take_request(10), 0u);
    Pvs_placement placement = residency.place(0u, 10, 3);
    EXPECT_EQ(placement.usable_frame, 10u); // a never used slot: at once
    EXPECT_EQ(residency.get_slot(0u), Pvs_residency::no_slot); // not resident before commit()
    residency.commit(0u, 10);
    EXPECT_EQ(residency.get_slot(0u), placement.slot);

    // The view moves away: page 0 leaves the table in frame 20, its slot is
    // reserved for page 5 and usable 3 frames later.
    residency.set_view(1, box_at(glm::uvec3{5u, 0u, 0u}), 20);
    ASSERT_EQ(residency.take_request(20), 5u);
    const uint64_t serial_before = residency.get_serial();
    placement = residency.place(5u, 20, 3);
    EXPECT_EQ(placement.evicted_page, 0u);
    EXPECT_EQ(placement.usable_frame, 23u);
    EXPECT_EQ(residency.get_slot(0u), Pvs_residency::no_slot);
    EXPECT_EQ(residency.get_slot(5u), Pvs_residency::no_slot);
    EXPECT_NE(residency.get_serial(), serial_before); // the eviction changed the table
    EXPECT_EQ(residency.get_resident_count(), 0u);
    EXPECT_TRUE(residency.is_in_flight(5u));
    // The policy refuses a commit before the frames in flight retired.
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(residency.commit(5u, 22), "");
    residency.commit(5u, placement.usable_frame);
    EXPECT_EQ(residency.get_slot(5u), placement.slot);
    EXPECT_EQ(residency.get_resident_count(), 1u);
}
