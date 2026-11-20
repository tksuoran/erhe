#pragma once

#include "background_work.hpp"

#include "erhe_task/executor.hpp"

#include <array>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace tf {
    class Executor;
}

namespace editor {

class Background_work_report
{
public:
    const char*            name{nullptr};
    Background_work_counts counts;
};

// One executor as reported: the priority report is read once, at construction (priorities do not change after startup).
class Executor_status_entry
{
public:
    const char*                        role{nullptr}; // "foreground" or "background"
    tf::Executor*                      executor{nullptr};
    int                                configured_thread_count{0}; // the setting the executor was made with
    erhe::task::Worker_priority_report priority_report;
    std::size_t                        priority_applied_count{0};
    std::vector<int>                   read_back_values;  // distinct, ascending
    std::string                        read_back_summary; // read_back_values for display

    [[nodiscard]] auto get_busy_worker_count() const -> std::size_t;
};

// Reports the editor's executors and the owners of background work (MCP get_executor_status and get_async_status,
// the Frame Pacing window's "Workers" section). Values are read when asked.
class Executor_status
{
public:
    static constexpr std::size_t c_foreground{0}; // indices of get_executors()
    static constexpr std::size_t c_background{1};

    Executor_status(
        tf::Executor& foreground,
        int           foreground_configured_thread_count,
        tf::Executor& background,
        int           background_configured_thread_count
    );

    [[nodiscard]] auto get_executors() const -> const std::array<Executor_status_entry, 2>&;
    // Clears and fills out with every registered owner, in registration order.
    void get_background_work(std::vector<Background_work_report>& out) const;
    // True when every owner reports nothing queued, running or ready.
    [[nodiscard]] auto is_background_idle() const -> bool;

    // ImGui time: the "Workers" section.
    void imgui();

private:
    friend class Background_work_owner;
    void register_owner  (Background_work_owner& owner);
    void unregister_owner(Background_work_owner& owner);

    std::array<Executor_status_entry, 2>  m_executors;
    mutable std::mutex                    m_mutex;
    std::vector<Background_work_owner*>   m_owners;     // guarded by m_mutex
    std::vector<Background_work_report>   m_ui_reports; // imgui() scratch
};

} // namespace editor
