#pragma once

#include <taskflow/taskflow.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace erhe::task {

enum class Worker_priority : unsigned int {
    normal = 0,
    lowered
};

[[nodiscard]] auto c_str(Worker_priority priority) -> const char*;

class Executor_create_info
{
public:
    std::string     name;
    std::size_t     thread_count{1};
    Worker_priority priority    {Worker_priority::normal};
};

class Worker_thread_report
{
public:
    std::size_t worker_index    {0};
    bool        priority_applied{false}; // the requested priority is in effect
    int         error_code      {0};     // errno or GetLastError() of a failed call
    bool        read_back_valid {false};
    int         read_back_value {0};     // nice (Linux, Android), qos_class_t (macOS), GetThreadPriority() (Windows)
};

class Worker_priority_report
{
public:
    std::string                       name;
    Worker_priority                   requested{Worker_priority::normal};
    std::vector<Worker_thread_report> workers; // indexed by worker id
};

// Names and prioritizes each worker before it runs a task (doc/erhe/task.md "Executors").
[[nodiscard]] auto make_executor(const Executor_create_info& create_info) -> std::unique_ptr<tf::Executor>;

// Empty for an executor not made by make_executor().
[[nodiscard]] auto get_worker_priority_report(const tf::Executor& executor) -> Worker_priority_report;

// The foreground and the background executor of an application (doc/erhe/task.md "Two executors").
class Executors
{
public:
    Executors(tf::Executor& foreground, tf::Executor& background);

    [[nodiscard]] auto get_foreground() const -> tf::Executor&;
    [[nodiscard]] auto get_background() const -> tf::Executor&;
    // The background executor on one of its workers, the foreground executor on any other thread.
    [[nodiscard]] auto get_inherited () const -> tf::Executor&;

private:
    tf::Executor& m_foreground;
    tf::Executor& m_background;
};

} // namespace erhe::task
