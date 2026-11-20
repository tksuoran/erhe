#pragma once

#include "erhe_verify/verify.hpp"

#include <cstddef>
#include <functional>
#include <mutex>

namespace editor {

class Executor_status;

class Background_work_counts
{
public:
    std::size_t queued {0};
    std::size_t running{0};
    std::size_t ready  {0}; // results waiting to be applied on the main thread
};

// Queued and running counts of an owner's tasks, shared with the tasks; on_started() and on_finished() run on the worker.
class Background_task_counter
{
public:
    void on_spawned()
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        ++m_queued;
    }
    void on_started()
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        ERHE_VERIFY(m_queued > 0);
        --m_queued;
        ++m_running;
    }
    void on_finished()
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        ERHE_VERIFY(m_running > 0);
        --m_running;
    }
    [[nodiscard]] auto get_counts() const -> Background_work_counts
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        return Background_work_counts{.queued = m_queued, .running = m_running};
    }

private:
    mutable std::mutex m_mutex;
    std::size_t        m_queued {0};
    std::size_t        m_running{0};
};

// An owner of background work, registered with Executor_status for its lifetime (doc/editor/editor.md "Worker executors").
class Background_work_owner
{
public:
    Background_work_owner(Executor_status& executor_status, const char* name, std::function<Background_work_counts()> get_counts);
    ~Background_work_owner() noexcept;
    Background_work_owner(const Background_work_owner&) = delete;
    auto operator=(const Background_work_owner&) -> Background_work_owner& = delete;
    Background_work_owner(Background_work_owner&&) = delete;
    auto operator=(Background_work_owner&&) -> Background_work_owner& = delete;

    [[nodiscard]] auto get_name  () const -> const char*;
    [[nodiscard]] auto get_counts() const -> Background_work_counts;

private:
    Executor_status&                        m_executor_status;
    const char*                             m_name;
    std::function<Background_work_counts()> m_get_counts;
};

} // namespace editor
