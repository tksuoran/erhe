#include "erhe_scene_renderer/pvs_page_streamer.hpp"

#include "erhe_scene/pvs_database.hpp"
#include "erhe_task/task.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <utility>

namespace erhe::scene_renderer {

Pvs_page_streamer::Pvs_page_streamer(Pvs_page_read_executor read_executor)
    : m_read_executor{std::move(read_executor)}
    , m_state        {std::make_shared<Shared_state>()}
{
}

Pvs_page_streamer::~Pvs_page_streamer() noexcept
{
    // A running read task finishes its current read and stops; it holds
    // the state, so nothing it touches goes away under it.
    const std::lock_guard<std::mutex> lock{m_state->mutex};
    m_state->stopped = true;
    m_state->requests.clear();
}

void Pvs_page_streamer::request(std::shared_ptr<const erhe::scene::Pvs_database> database, const uint64_t generation, const uint32_t page)
{
    ERHE_VERIFY(database);
    bool start_task = false;
    {
        const std::lock_guard<std::mutex> lock{m_state->mutex};
        m_state->requests.push_back(Request{.database = std::move(database), .generation = generation, .page = page});
        if (!m_state->task_running) {
            m_state->task_running = true;
            start_task = true;
        }
    }
    if (!start_task) {
        return;
    }
    // The drain task never re-spawns, so its executor is the one chosen here.
    tf::Executor* const executor = m_read_executor ? m_read_executor() : nullptr;
    if (executor == nullptr) {
        drain(m_state);
        return;
    }
    std::shared_ptr<Shared_state> state = m_state;
    erhe::task::spawn(*executor, [state]() { drain(state); });
}

void Pvs_page_streamer::drain(const std::shared_ptr<Shared_state>& state)
{
    std::unique_lock<std::mutex> lock{state->mutex};
    while (!state->stopped && !state->requests.empty()) {
        Request request = std::move(state->requests.front());
        state->requests.erase(state->requests.begin());
        std::vector<uint32_t> words;
        if (!state->free_buffers.empty()) {
            words = std::move(state->free_buffers.back());
            state->free_buffers.pop_back();
        }
        state->reading = true;
        lock.unlock();

        // The read: the in-memory copy or a range of the bits source.
        words.resize(request.database->get_words_per_page());
        const bool ok = request.database->read_page_words(request.page, std::span<uint32_t>{words});
        request.database.reset();

        lock.lock();
        state->reading = false;
        state->results.push_back(Pvs_page_read{.generation = request.generation, .page = request.page, .ok = ok, .words = std::move(words)});
    }
    state->task_running = false;
}

auto Pvs_page_streamer::take_result(Pvs_page_read& out) -> bool
{
    const std::lock_guard<std::mutex> lock{m_state->mutex};
    if (m_state->results.empty()) {
        return false;
    }
    Pvs_page_read& front = m_state->results.front();
    out.generation = front.generation;
    out.page       = front.page;
    out.ok         = front.ok;
    std::swap(out.words, front.words);
    m_state->free_buffers.push_back(std::move(front.words));
    m_state->results.erase(m_state->results.begin());
    return true;
}

void Pvs_page_streamer::cancel()
{
    const std::lock_guard<std::mutex> lock{m_state->mutex};
    m_state->requests.clear();
    for (Pvs_page_read& result : m_state->results) {
        m_state->free_buffers.push_back(std::move(result.words));
    }
    m_state->results.clear();
}

auto Pvs_page_streamer::get_counts() const -> Pvs_page_streamer_counts
{
    const std::lock_guard<std::mutex> lock{m_state->mutex};
    return Pvs_page_streamer_counts{
        .queued  = m_state->requests.size(),
        .running = m_state->reading ? std::size_t{1} : std::size_t{0},
        .ready   = m_state->results.size()
    };
}

auto Pvs_page_upload_queue::push(
    const uint64_t                  frame_index,
    const uint64_t                  generation,
    const uint32_t                  page,
    const uint32_t                  slot,
    const std::span<const uint32_t> words
) -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (m_count == capacity) {
        return false;
    }
    Pvs_page_upload& entry = m_entries[(m_first + m_count) % capacity];
    entry.frame_index = frame_index;
    entry.generation  = generation;
    entry.page        = page;
    entry.slot        = slot;
    entry.words.assign(words.begin(), words.end());
    ++m_count;
    return true;
}

auto Pvs_page_upload_queue::is_full() const -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_count == capacity;
}

auto Pvs_page_upload_queue::get_size() const -> std::size_t
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_count;
}

} // namespace erhe::scene_renderer
