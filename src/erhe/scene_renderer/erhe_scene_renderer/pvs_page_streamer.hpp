#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace tf {
    class Executor;
}
namespace erhe::scene {
    class Pvs_database;
}

namespace erhe::scene_renderer {

// One page read of the PVS page streamer: the words of a stored page of
// the database (Pvs_database::read_page_words()), or ok = false.
class Pvs_page_read
{
public:
    uint64_t              generation{0};
    uint32_t              page      {0};
    bool                  ok        {false};
    std::vector<uint32_t> words;
};

// The page reads of the PVS page streaming (doc/plans/pvs.md "Residency
// and streaming"; the Lightmap_streamer pattern): a read task on the task
// executor (erhe::task::spawn(); one task at a time, which drains the
// queued requests) reads requested pages by range from the database's bits
// source (or from its in-memory copy after a bake) and hands the words back
// through a mutex-protected queue. The task touches no scene or GPU state;
// it holds the shared queue state and the requests' databases by
// shared_ptr, so it may outlive the streamer. The owner (Draw_list_scene)
// calls every member on the main thread. The word buffers are reused (a
// steady state allocates nothing).
class Pvs_page_streamer
{
public:
    // executor null: each request is read inline.
    explicit Pvs_page_streamer(tf::Executor* executor);
    ~Pvs_page_streamer() noexcept;
    Pvs_page_streamer(const Pvs_page_streamer&)            = delete;
    Pvs_page_streamer& operator=(const Pvs_page_streamer&) = delete;

    // Queues the read of one page of database, tagged with generation.
    void request(std::shared_ptr<const erhe::scene::Pvs_database> database, uint64_t generation, uint32_t page);
    // The oldest finished read, if any, into out (out's previous word
    // buffer goes back to the pool).
    [[nodiscard]] auto take_result(Pvs_page_read& out) -> bool;
    // Drops the queued requests and finished reads (a read in progress
    // still finishes; its generation tells the owner to drop it).
    void cancel();

private:
    class Request
    {
    public:
        std::shared_ptr<const erhe::scene::Pvs_database> database;
        uint64_t                                         generation{0};
        uint32_t                                         page      {0};
    };

    // Shared by the streamer and its read task.
    class Shared_state
    {
    public:
        std::mutex                         mutex;
        bool                               task_running{false};
        bool                               stopped     {false};
        std::vector<Request>               requests;
        std::vector<Pvs_page_read>         results;
        std::vector<std::vector<uint32_t>> free_buffers;
    };

    // The read task's body: reads until the queue is empty.
    static void drain(const std::shared_ptr<Shared_state>& state);

    tf::Executor*                 m_executor{nullptr};
    std::shared_ptr<Shared_state> m_state;
};

// One page upload of a frame (Pvs_page_upload_queue): the prepare phase of
// frame_index placed page in slot of residency generation; the render
// phase writes the words into the slot before the frame's classification.
class Pvs_page_upload
{
public:
    uint64_t              frame_index{0};
    uint64_t              generation {0};
    uint32_t              page       {0};
    uint32_t              slot       {0};
    std::vector<uint32_t> words;
};

// The page uploads from the prepare phase (main thread) to the render
// phase (render thread): a fixed ring of capacity entries behind a mutex,
// in frame order. The render phase applies every upload of its frame and
// the frames before it, so an upload of a frame that classified nothing is
// applied by the next frame that does (the page tables of the later frames
// name its slot). The word buffers keep their capacity.
class Pvs_page_upload_queue
{
public:
    static constexpr std::size_t capacity = 4;

    // Main thread: false when the ring is full (the upload waits).
    [[nodiscard]] auto push(uint64_t frame_index, uint64_t generation, uint32_t page, uint32_t slot, std::span<const uint32_t> words) -> bool;
    [[nodiscard]] auto is_full() const -> bool;
    [[nodiscard]] auto get_size() const -> std::size_t;
    // Render thread: calls apply for every upload of frame_index or before,
    // oldest first, and removes them.
    template <typename Apply>
    void consume(const uint64_t frame_index, Apply&& apply)
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        while ((m_count > 0) && (m_entries[m_first].frame_index <= frame_index)) {
            apply(static_cast<const Pvs_page_upload&>(m_entries[m_first]));
            m_first = (m_first + 1) % capacity;
            --m_count;
        }
    }

private:
    mutable std::mutex                    m_mutex;
    std::array<Pvs_page_upload, capacity> m_entries{};
    std::size_t                           m_first{0};
    std::size_t                           m_count{0};
};

} // namespace erhe::scene_renderer
