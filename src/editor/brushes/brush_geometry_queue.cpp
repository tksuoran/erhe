#include "brushes/brush_geometry_queue.hpp"

#include "brushes/brush.hpp"
#include "editor_log.hpp"

#include "erhe_task/task.hpp"
#include "erhe_verify/verify.hpp"

#include <taskflow/taskflow.hpp>

namespace editor {

Brush_geometry_queue_state::Brush_geometry_queue_state(tf::Executor& executor)
    : m_executor{executor}
{
    // One worker is left for the rest of the editor's task work (D4).
    const std::size_t worker_count = static_cast<std::size_t>(executor.num_workers());
    m_max_in_flight = (worker_count > 1) ? (worker_count - 1) : std::size_t{1};
}

void Brush_geometry_queue_state::request(const std::shared_ptr<Brush>& brush, const std::string& name)
{
    if (!brush) {
        return;
    }
    std::size_t tasks_to_start = 0;
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        if (m_stopped) {
            return;
        }
        // A brush that is already queued moves to the front (R5); entries whose
        // brush is gone are dropped while the list is walked anyway.
        for (std::deque<Brush_geometry_request>::iterator i = m_pending.begin(); i != m_pending.end(); ) {
            const std::shared_ptr<Brush> pending = i->brush.lock();
            if (!pending || (pending == brush)) {
                i = m_pending.erase(i);
            } else {
                ++i;
            }
        }
        m_pending.push_front(Brush_geometry_request{.brush = brush, .name = name});
        if (m_in_flight < m_max_in_flight) {
            tasks_to_start = std::min(m_max_in_flight - m_in_flight, m_pending.size());
            m_in_flight += tasks_to_start;
        }
    }
    spawn_tasks(tasks_to_start);
}

void Brush_geometry_queue_state::spawn_tasks(const std::size_t task_count)
{
    if (task_count == 0) {
        return;
    }
    // Every task co-owns the state, so the state outlives the queue whenever a
    // task is still running (D8).
    const std::shared_ptr<Brush_geometry_queue_state> self = shared_from_this();
    for (std::size_t i = 0; i < task_count; ++i) {
        erhe::task::spawn(
            m_executor,
            [self]() -> void
            {
                self->work();
            }
        );
    }
}

void Brush_geometry_queue_state::work()
{
    for (;;) {
        std::shared_ptr<Brush> brush;
        std::string            name;
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            while (!m_stopped && !m_pending.empty()) {
                brush = m_pending.front().brush.lock();
                name  = m_pending.front().name;
                m_pending.pop_front();
                if (brush) {
                    break;
                }
            }
            if (!brush || m_stopped) {
                ERHE_VERIFY(m_in_flight > 0);
                --m_in_flight;
                return;
            }
        }
        // Outside the queue mutex: the brush's own mutex is the only lock held
        // while its generator runs, and the slot skips a brush that the main
        // thread or another task has already taken (R8).
        static_cast<void>(brush->prepare_geometry_if_queued(name));
    }
}

void Brush_geometry_queue_state::stop()
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_stopped = true;
    m_pending.clear();
}

auto Brush_geometry_queue_state::get_pending_count() const -> std::size_t
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_pending.size();
}

auto Brush_geometry_queue_state::get_in_flight_count() const -> std::size_t
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_in_flight;
}

auto Brush_geometry_queue_state::get_max_in_flight() const -> std::size_t
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_max_in_flight;
}

Brush_geometry_queue::Brush_geometry_queue(tf::Executor& executor)
    : m_state{std::make_shared<Brush_geometry_queue_state>(executor)}
{
    log_brush->trace("Brush geometry queue: up to {} preparation tasks in flight", m_state->get_max_in_flight());
}

Brush_geometry_queue::~Brush_geometry_queue() noexcept
{
    m_state->stop();
}

auto Brush_geometry_queue::get_state() const -> const std::shared_ptr<Brush_geometry_queue_state>&
{
    return m_state;
}

}
