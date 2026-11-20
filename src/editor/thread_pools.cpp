#include "thread_pools.hpp"

#include "editor_log.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_raytrace/raytrace_executor.hpp"

#if defined(ERHE_TBB)
#   include <tbb/blocked_range.h>
#   include <tbb/global_control.h>
#   include <tbb/parallel_for.h>
#   include <tbb/partitioner.h>
#   include <tbb/task_arena.h>
#   include <latch>
#endif

#include <algorithm>
#include <cstddef>

namespace editor {

namespace {

#if defined(ERHE_TBB)
// oneTBB launches a worker on demand, from the thread that asks for it (or from a worker it launched), and keeps it
// for the process. One task per thread, each held until all have arrived, makes every worker launch now.
void start_tbb_workers()
{
    const std::size_t thread_count = std::min(
        static_cast<std::size_t>(std::max(tbb::this_task_arena::max_concurrency(), 1)),
        std::max(tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism), std::size_t{1})
    );
    std::latch all_arrived{static_cast<std::ptrdiff_t>(thread_count)};
    tbb::parallel_for(
        tbb::blocked_range<std::size_t>{0, thread_count, 1},
        [&all_arrived](const tbb::blocked_range<std::size_t>&) {
            all_arrived.arrive_and_wait();
        },
        tbb::simple_partitioner{}
    );
    log_startup->info("Thread pools: {} oneTBB workers started", thread_count - 1);
}
#endif

} // anonymous namespace

void start_outside_thread_pools()
{
    ERHE_PROFILE_FUNCTION();

    erhe::raytrace::start_thread_pool();
#if defined(ERHE_TBB)
    start_tbb_workers();
#endif
}

} // namespace editor
