#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace tf {
    class Executor;
}

namespace editor {

class Brush;

// One pending tier 2 preparation request (doc/editor/brushes.md G7): the
// brush is held weakly, so a task that finds it gone returns, and the
// name is copied on the requesting thread, so the task never reads the brush's
// name string while another thread may be renaming it.
class Brush_geometry_request final
{
public:
    std::weak_ptr<Brush> brush;
    std::string          name;
};

// The queue's shared state (doc/editor/brushes.md G5, G6). It is held by a shared_ptr from two
// sides: the Brush_geometry_queue that owns the queue, and every preparation
// task in flight. A task therefore never touches a destroyed queue - the queue
// destructor only calls stop(), which refuses further requests and drops the
// pending list, and the last task to finish releases the state.
//
// A brush reaches the queue through the weak_ptr its Brush_data carries, so a
// brush that outlives the queue simply prepares its geometry on the calling
// thread (tier 1) instead.
class Brush_geometry_queue_state final : public std::enable_shared_from_this<Brush_geometry_queue_state>
{
public:
    explicit Brush_geometry_queue_state(tf::Executor& executor);

    Brush_geometry_queue_state           (const Brush_geometry_queue_state&) = delete;
    Brush_geometry_queue_state& operator=(const Brush_geometry_queue_state&) = delete;
    Brush_geometry_queue_state           (Brush_geometry_queue_state&&)      = delete;
    Brush_geometry_queue_state& operator=(Brush_geometry_queue_state&&)      = delete;

    // Puts the brush at the front of the queue - most recently requested is
    // served first - removing an entry it already had, and starts a task
    // when fewer than max_in_flight are running.
    void request(const std::shared_ptr<Brush>& brush, const std::string& name);

    // Refuses further requests and drops the pending list. Brushes left
    // `queued` are prepared by their first tier 1 consumer.
    void stop();

    [[nodiscard]] auto get_pending_count  () const -> std::size_t;
    [[nodiscard]] auto get_in_flight_count() const -> std::size_t;
    [[nodiscard]] auto get_max_in_flight  () const -> std::size_t;

private:
    // Spawns `task_count` tasks; called with m_mutex released, after the count
    // has been added to m_in_flight under it.
    void spawn_tasks(std::size_t task_count);

    // One task body: prepares the most recently requested brush, then the next
    // one, until the queue runs dry or stop() has been called.
    void work();

    tf::Executor&                     m_executor;
    mutable std::mutex                m_mutex;
    std::deque<Brush_geometry_request> m_pending;          // front = most recently requested
    std::size_t                       m_in_flight    {0};
    std::size_t                       m_max_in_flight{1};
    bool                              m_stopped      {false};
};

// The preparation queue of the brush palette (doc/editor/brushes.md G5), owned by Scene_builder next
// to the palette. Tier 2 consumers reach it through Brush::request_geometry().
class Brush_geometry_queue final
{
public:
    explicit Brush_geometry_queue(tf::Executor& executor);
    ~Brush_geometry_queue() noexcept;

    Brush_geometry_queue           (const Brush_geometry_queue&) = delete;
    Brush_geometry_queue& operator=(const Brush_geometry_queue&) = delete;
    Brush_geometry_queue           (Brush_geometry_queue&&)      = delete;
    Brush_geometry_queue& operator=(Brush_geometry_queue&&)      = delete;

    // The state a Brush_data carries weakly.
    [[nodiscard]] auto get_state() const -> const std::shared_ptr<Brush_geometry_queue_state>&;

private:
    std::shared_ptr<Brush_geometry_queue_state> m_state;
};

}
