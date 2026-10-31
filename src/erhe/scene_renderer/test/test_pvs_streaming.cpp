// The PVS page streaming's queues (doc/plans/pvs.md "Residency and
// streaming"): Pvs_page_upload_queue (frame order, the full ring, the
// render phase dropping the uploads of a previous generation) and
// Pvs_page_streamer reading pages inline (no executor).

#include "erhe_scene_renderer/pvs_page_streamer.hpp"

#include "erhe_scene/pvs_database.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

using erhe::scene_renderer::Pvs_page_read;
using erhe::scene_renderer::Pvs_page_streamer;
using erhe::scene_renderer::Pvs_page_upload;
using erhe::scene_renderer::Pvs_page_upload_queue;

class Applied
{
public:
    uint64_t frame_index{0};
    uint64_t generation {0};
    uint32_t page       {0};
    uint32_t slot       {0};
    uint32_t first_word {0};
};

// A 2 x 1 x 1 grid of 1 m cells, page size 1 (two pages, both stored), one
// column: page 0's cell sees it (word 1), page 1's does not (word 0).
[[nodiscard]] auto make_streaming_database() -> std::shared_ptr<const erhe::scene::Pvs_database>
{
    erhe::scene::Pvs_header header{};
    header.scene_id    = "streaming-test";
    header.cell_size   = 1.0f;
    header.cell_counts = glm::uvec3{2u, 1u, 1u};
    header.page_size   = 1;
    erhe::scene::Pvs_column column{};
    column.node_uid        = "node";
    column.world_transform = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    column.world_bounds    = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    column.transform_hash  = erhe::scene::compute_pvs_transform_hash(column.world_transform, column.world_bounds);
    std::vector<erhe::scene::Pvs_page_entry> pages(2);
    pages[0].word_offset    = 0;
    pages[0].cells_with_pvs = 1u;
    pages[1].word_offset    = 1;
    pages[1].cells_with_pvs = 1u;
    std::vector<uint32_t> bits{1u, 0u};
    std::vector<erhe::scene::Pvs_column> columns{column};
    EXPECT_EQ(erhe::scene::Pvs_database::validate(header, columns, pages, bits), std::string{});
    return std::make_shared<const erhe::scene::Pvs_database>(std::move(header), std::move(columns), std::move(pages), std::move(bits));
}

} // anonymous namespace

TEST(Pvs_page_upload_queue, applies_in_frame_order_up_to_the_frame)
{
    Pvs_page_upload_queue queue;
    for (uint32_t i = 0; i < Pvs_page_upload_queue::capacity; ++i) {
        const std::vector<uint32_t> words{100u + i, 200u + i};
        EXPECT_TRUE(queue.push(10 + i, 1, i, i, std::span<const uint32_t>{words}));
    }
    // The ring is full: the next upload waits.
    const std::vector<uint32_t> words{0u, 0u};
    EXPECT_TRUE(queue.is_full());
    EXPECT_FALSE(queue.push(20, 1, 9, 9, std::span<const uint32_t>{words}));

    std::vector<Applied> applied;
    const auto apply = [&applied](const Pvs_page_upload& upload) {
        applied.push_back(Applied{upload.frame_index, upload.generation, upload.page, upload.slot, upload.words.at(0)});
    };
    queue.consume(11, apply); // frames 10 and 11, not 12 and 13
    ASSERT_EQ(applied.size(), 2u);
    EXPECT_EQ(applied[0].frame_index, 10u);
    EXPECT_EQ(applied[1].frame_index, 11u);
    EXPECT_EQ(applied[1].first_word, 101u);
    EXPECT_EQ(queue.get_size(), 2u);

    // Room again; the ring wraps.
    const std::vector<uint32_t> more{300u, 301u};
    EXPECT_TRUE(queue.push(14, 2, 7, 3, std::span<const uint32_t>{more}));
    queue.consume(100, apply);
    ASSERT_EQ(applied.size(), 5u);
    EXPECT_EQ(applied[2].frame_index, 12u);
    EXPECT_EQ(applied[3].frame_index, 13u);
    EXPECT_EQ(applied[4].frame_index, 14u);
    EXPECT_EQ(applied[4].generation, 2u);
    EXPECT_EQ(applied[4].page, 7u);
    EXPECT_EQ(applied[4].slot, 3u);
    EXPECT_EQ(applied[4].first_word, 300u);
    EXPECT_EQ(queue.get_size(), 0u);
}

TEST(Pvs_page_upload_queue, the_render_phase_drops_uploads_of_a_previous_generation)
{
    // Draw_list_scene::apply_pvs_uploads(): every upload of the frame and
    // before is consumed, those of another generation than the frame's
    // resident slots are dropped.
    Pvs_page_upload_queue queue;
    const std::vector<uint32_t> words{1u};
    EXPECT_TRUE(queue.push(5, 1, 0, 0, std::span<const uint32_t>{words}));
    EXPECT_TRUE(queue.push(6, 2, 0, 0, std::span<const uint32_t>{words}));
    const uint64_t current_generation = 2;
    std::vector<uint64_t> written;
    queue.consume(6, [&written, current_generation](const Pvs_page_upload& upload) {
        if (upload.generation == current_generation) {
            written.push_back(upload.frame_index);
        }
    });
    EXPECT_EQ(written, (std::vector<uint64_t>{6u}));
    EXPECT_EQ(queue.get_size(), 0u);
}

TEST(Pvs_page_streamer, reads_requested_pages_and_cancel_drops_results)
{
    const std::shared_ptr<const erhe::scene::Pvs_database> database = make_streaming_database();
    Pvs_page_streamer streamer{nullptr}; // inline reads
    Pvs_page_read     read{};
    EXPECT_FALSE(streamer.take_result(read));

    streamer.request(database, 7, 1);
    ASSERT_TRUE(streamer.take_result(read));
    EXPECT_TRUE(read.ok);
    EXPECT_EQ(read.generation, 7u);
    EXPECT_EQ(read.page, 1u);
    EXPECT_EQ(read.words, (std::vector<uint32_t>{database->get_bits().at(1)}));
    EXPECT_FALSE(streamer.take_result(read));

    // A page out of range fails the read.
    streamer.request(database, 7, 5);
    ASSERT_TRUE(streamer.take_result(read));
    EXPECT_FALSE(read.ok);

    // Cancelled before it is taken: gone.
    streamer.request(database, 8, 0);
    streamer.cancel();
    EXPECT_FALSE(streamer.take_result(read));
}
