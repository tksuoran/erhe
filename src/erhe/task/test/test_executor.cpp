#include "erhe_task/executor.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <memory>

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#elif defined(__APPLE__)
#   include <pthread/qos.h>
#elif defined(__linux__)
#   include <sys/resource.h>
#   include <sys/types.h>
#   include <unistd.h>
#endif

namespace {

constexpr std::size_t worker_count = 3;

auto make_report(const erhe::task::Worker_priority priority) -> erhe::task::Worker_priority_report
{
    const std::unique_ptr<tf::Executor> executor = erhe::task::make_executor(
        erhe::task::Executor_create_info{
            .name         = "Test worker",
            .thread_count = worker_count,
            .priority     = priority
        }
    );
    EXPECT_EQ(executor->num_workers(), worker_count);
    return erhe::task::get_worker_priority_report(*executor.get());
}

void expect_complete(const erhe::task::Worker_priority_report& report, const erhe::task::Worker_priority priority)
{
    EXPECT_EQ(report.name, "Test worker");
    EXPECT_EQ(report.requested, priority);
    ASSERT_EQ(report.workers.size(), worker_count);
    for (std::size_t i = 0; i < report.workers.size(); ++i) {
        EXPECT_EQ(report.workers[i].worker_index, i);
        EXPECT_TRUE(report.workers[i].priority_applied);
        EXPECT_EQ(report.workers[i].error_code, 0);
        EXPECT_TRUE(report.workers[i].read_back_valid);
    }
}

#if defined(__linux__)
// Threads inherit the nice value of the thread that creates them.
auto get_own_nice() -> int
{
    return getpriority(PRIO_PROCESS, static_cast<id_t>(gettid()));
}
#endif

} // anonymous namespace

TEST(Executor, lowered_workers_read_back_the_lowered_priority)
{
    const erhe::task::Worker_priority_report report = make_report(erhe::task::Worker_priority::lowered);
    expect_complete(report, erhe::task::Worker_priority::lowered);
    for (const erhe::task::Worker_thread_report& worker : report.workers) {
#if defined(_WIN32)
        EXPECT_EQ(worker.read_back_value, THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__APPLE__)
        EXPECT_EQ(worker.read_back_value, static_cast<int>(QOS_CLASS_UTILITY));
#elif defined(__linux__)
        EXPECT_EQ(worker.read_back_value, std::max(10, get_own_nice()));
#endif
    }
}

TEST(Executor, normal_workers_keep_the_normal_priority)
{
    const erhe::task::Worker_priority_report report = make_report(erhe::task::Worker_priority::normal);
    expect_complete(report, erhe::task::Worker_priority::normal);
    for (const erhe::task::Worker_thread_report& worker : report.workers) {
#if defined(_WIN32)
        EXPECT_EQ(worker.read_back_value, THREAD_PRIORITY_NORMAL);
#elif defined(__APPLE__)
        EXPECT_NE(worker.read_back_value, static_cast<int>(QOS_CLASS_UTILITY));
#elif defined(__linux__)
        EXPECT_EQ(worker.read_back_value, get_own_nice());
#endif
    }
}

TEST(Executor, report_of_an_executor_not_made_by_make_executor_is_empty)
{
    tf::Executor executor{1};
    const erhe::task::Worker_priority_report report = erhe::task::get_worker_priority_report(executor);
    EXPECT_TRUE(report.name.empty());
    EXPECT_TRUE(report.workers.empty());
}
