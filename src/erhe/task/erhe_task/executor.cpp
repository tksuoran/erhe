#include "erhe_task/executor.hpp"
#include "erhe_task/task.hpp"
#include "erhe_task/task_log.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <utility>

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#elif defined(__APPLE__)
#   include <pthread.h>
#   include <pthread/qos.h>
#elif defined(__linux__)
#   include <cerrno>
#   include <pthread.h>
#   include <sys/resource.h>
#   include <sys/types.h>
#   include <unistd.h>
#endif

namespace erhe::task {

auto c_str(const Worker_priority priority) -> const char*
{
    switch (priority) {
        case Worker_priority::normal:  return "normal";
        case Worker_priority::lowered: return "lowered";
        default:                       return "?";
    }
}

namespace {

#if defined(_WIN32)

void set_os_thread_name(const std::string& name)
{
    std::wstring wide_name(name.begin(), name.end()); // names are ASCII
    static_cast<void>(SetThreadDescription(GetCurrentThread(), wide_name.c_str()));
}

auto read_thread_priority(int& value) -> bool
{
    const int priority = GetThreadPriority(GetCurrentThread());
    if (priority == THREAD_PRIORITY_ERROR_RETURN) {
        return false;
    }
    value = priority;
    return true;
}

void lower_thread_priority(Worker_thread_report& report)
{
    int current = 0;
    if (read_thread_priority(current) && (current <= THREAD_PRIORITY_BELOW_NORMAL)) {
        report.priority_applied = true; // never raise a thread that is already lower
        return;
    }
    if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL) == 0) {
        report.error_code = static_cast<int>(GetLastError());
        return;
    }
    report.priority_applied = true;
}

#elif defined(__APPLE__)

void set_os_thread_name(const std::string& name)
{
    static_cast<void>(pthread_setname_np(name.c_str()));
}

auto read_thread_priority(int& value) -> bool
{
    qos_class_t qos_class         = QOS_CLASS_UNSPECIFIED;
    int         relative_priority = 0;
    if (pthread_get_qos_class_np(pthread_self(), &qos_class, &relative_priority) != 0) {
        return false;
    }
    value = static_cast<int>(qos_class);
    return true;
}

void lower_thread_priority(Worker_thread_report& report)
{
    int current = 0;
    if (
        read_thread_priority(current) &&
        (current != static_cast<int>(QOS_CLASS_UNSPECIFIED)) &&
        (current <= static_cast<int>(QOS_CLASS_UTILITY))
    ) {
        report.priority_applied = true; // never raise a thread that is already lower
        return;
    }
    const int result = pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
    if (result != 0) {
        report.error_code = result;
        return;
    }
    report.priority_applied = true;
}

#elif defined(__linux__)

// The kernel limits a thread name to 15 characters; the worker index is kept.
void set_os_thread_name(const std::string& name)
{
    constexpr std::size_t max_length = 15;
    std::string os_name = name;
    if (os_name.size() > max_length) {
        const std::size_t space = name.find_last_of(' ');
        const std::string suffix = (space != std::string::npos) ? name.substr(space) : std::string{};
        os_name = name.substr(0, max_length - std::min(suffix.size(), max_length)) + suffix;
        os_name.resize(std::min(os_name.size(), max_length));
    }
    static_cast<void>(pthread_setname_np(pthread_self(), os_name.c_str()));
}

auto read_thread_priority(int& value) -> bool
{
    errno = 0;
    const int nice_value = getpriority(PRIO_PROCESS, static_cast<id_t>(gettid()));
    if ((nice_value == -1) && (errno != 0)) {
        return false;
    }
    value = nice_value;
    return true;
}

// A thread id makes setpriority() set the nice value of that one thread.
void lower_thread_priority(Worker_thread_report& report)
{
    constexpr int lowered_nice = 10;
    int current = 0;
    if (read_thread_priority(current) && (current >= lowered_nice)) {
        report.priority_applied = true; // never raise a thread that is already lower
        return;
    }
    if (setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), lowered_nice) != 0) {
        report.error_code = errno;
        return;
    }
    report.priority_applied = true;
}

#else

void set_os_thread_name(const std::string&)
{
}

auto read_thread_priority(int&) -> bool
{
    return false;
}

void lower_thread_priority(Worker_thread_report&)
{
}

#endif

// Written once by the worker's own scheduler prologue, read only by that thread.
thread_local Worker_priority t_worker_priority{Worker_priority::normal};
thread_local std::size_t     t_worker_index   {0};

class Executor_worker_state
{
public:
    std::mutex                        mutex;
    std::condition_variable           all_reported;
    std::string                       name;
    Worker_priority                   requested{Worker_priority::normal};
    std::size_t                       reported_count{0};
    bool                              failure_logged{false};
    std::vector<Worker_thread_report> workers;
};

class Worker_interface final : public tf::WorkerInterface
{
public:
    explicit Worker_interface(std::shared_ptr<Executor_worker_state> state)
        : m_state{std::move(state)}
    {
    }

    // Runs on the worker thread before its scheduling loop.
    void scheduler_prologue(tf::Worker& worker) override
    {
        const std::size_t worker_index = worker.id();
        const std::string thread_name  = fmt::format("{} {}", m_state->name, worker_index);
        ERHE_PROFILE_THREAD_NAME(thread_name.c_str());
        set_os_thread_name(thread_name);

        t_worker_priority = m_state->requested;
        t_worker_index    = worker_index;

        Worker_thread_report report{.worker_index = worker_index};
        if (m_state->requested == Worker_priority::lowered) {
            lower_thread_priority(report);
        } else {
            report.priority_applied = true;
        }
        report.read_back_valid = read_thread_priority(report.read_back_value);

        bool log_failure = false;
        {
            const std::lock_guard<std::mutex> lock{m_state->mutex};
            m_state->workers.at(worker_index) = report;
            if (!report.priority_applied && !m_state->failure_logged) {
                m_state->failure_logged = true;
                log_failure = true;
            }
            ++m_state->reported_count;
            if (m_state->reported_count == m_state->workers.size()) {
                m_state->all_reported.notify_all();
            }
        }
        if (log_failure) {
            log_task->warn(
                "{}: lowering the worker thread priority failed (error {}); the workers run at normal priority",
                thread_name, report.error_code
            );
        }
    }

    void scheduler_epilogue(tf::Worker& worker, std::exception_ptr exception) override
    {
        if (!exception) {
            return;
        }
        try {
            std::rethrow_exception(exception);
        } catch (const std::exception& e) {
            log_task->error("{} {}: exception left the scheduling loop: {}", m_state->name, worker.id(), e.what());
        } catch (...) {
            log_task->error("{} {}: unknown exception left the scheduling loop", m_state->name, worker.id());
        }
    }

private:
    std::shared_ptr<Executor_worker_state> m_state;
};

class Executor_registry
{
public:
    std::mutex mutex;
    std::vector<std::pair<const tf::Executor*, std::weak_ptr<Executor_worker_state>>> entries;
};

auto get_registry() -> Executor_registry&
{
    static Executor_registry registry;
    return registry;
}

} // anonymous namespace

auto make_executor(const Executor_create_info& create_info) -> std::unique_ptr<tf::Executor>
{
    ERHE_VERIFY(create_info.thread_count > 0);

    std::shared_ptr<Executor_worker_state> state = std::make_shared<Executor_worker_state>();
    state->name      = create_info.name;
    state->requested = create_info.priority;
    state->workers.resize(create_info.thread_count);

    std::unique_ptr<tf::Executor> executor = std::make_unique<tf::Executor>(
        create_info.thread_count,
        std::make_shared<Worker_interface>(state)
    );

    // The report is complete when this returns.
    {
        std::unique_lock<std::mutex> lock{state->mutex};
        state->all_reported.wait(lock, [&state]() { return state->reported_count == state->workers.size(); });
    }

    // The interface, and with it the state, is released when the executor joins its workers.
    Executor_registry& registry = get_registry();
    const std::lock_guard<std::mutex> lock{registry.mutex};
    std::erase_if(
        registry.entries,
        [&executor](const std::pair<const tf::Executor*, std::weak_ptr<Executor_worker_state>>& entry) {
            return entry.second.expired() || (entry.first == executor.get());
        }
    );
    registry.entries.emplace_back(executor.get(), state);
    return executor;
}

auto get_worker_priority_report(const tf::Executor& executor) -> Worker_priority_report
{
    std::shared_ptr<Executor_worker_state> state;
    {
        Executor_registry& registry = get_registry();
        const std::lock_guard<std::mutex> lock{registry.mutex};
        for (const std::pair<const tf::Executor*, std::weak_ptr<Executor_worker_state>>& entry : registry.entries) {
            if (entry.first == &executor) {
                state = entry.second.lock();
                break;
            }
        }
    }
    if (!state) {
        return Worker_priority_report{};
    }
    const std::lock_guard<std::mutex> lock{state->mutex};
    return Worker_priority_report{
        .name      = state->name,
        .requested = state->requested,
        .workers   = state->workers
    };
}

void verify_executor_may_spawn(const tf::Executor& target, const std::source_location& location)
{
    if (t_worker_priority != Worker_priority::lowered) {
        return;
    }
    if (target.this_worker_id() >= 0) {
        return; // the calling thread is a worker of target
    }
    ERHE_FATAL(
        "task spawn at %s:%u on lowered priority worker %u onto another executor; a background worker spawns onto its own executor only",
        location.file_name(),
        static_cast<unsigned int>(location.line()),
        static_cast<unsigned int>(t_worker_index)
    );
}

Executors::Executors(tf::Executor& foreground, tf::Executor& background)
    : m_foreground{foreground}
    , m_background{background}
{
    ERHE_VERIFY(&foreground != &background);
}

auto Executors::get_foreground() const -> tf::Executor&
{
    return m_foreground;
}

auto Executors::get_background() const -> tf::Executor&
{
    return m_background;
}

auto Executors::get_inherited() const -> tf::Executor&
{
    return (m_background.this_worker_id() >= 0) ? m_background : m_foreground;
}

} // namespace erhe::task
