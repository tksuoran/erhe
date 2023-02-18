#include "erhe/concurrency/thread_pool.hpp"

#include <concurrentqueue.h>

#include <chrono>

namespace erhe::concurrency {

using std::chrono::high_resolution_clock;
using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::milliseconds;

// ------------------------------------------------------------
// Thread_pool
// ------------------------------------------------------------


struct Thread_pool::Task_queue
{
    using Task = Thread_pool::Task;

    moodycamel::ConcurrentQueue<Task> tasks;
};

Thread_pool::Thread_pool(size_t size)
    : m_queues      {nullptr}
    , m_static_queue{this, int(Priority::NORMAL), "static"}
    , m_threads     {size}
{
    m_queues = new Task_queue[3];

    // NOTE: let OS scheduler shuffle tasks as it sees fit
    //       this gives better performance overall UNTIL we have some practical
    //       use for the affinity (eg. dependent tasks using same cache)
    //const bool affinity = false;//std::thread::hardware_concurrency() > 1;
    //if (affinity) {
    //    set_current_thread_affinity(0);
    //}

    for (size_t i = 0; i < size; ++i) {
        m_threads[i] = std::thread([this, i]
        {
            thread(i);
        });

#if defined(MANGO_PLATFORM_WINDOWS)
        if (concurrency > 64) {
            // HACK: work around Windows 64 logical processor per ProcessorGroup limitation
            GROUP_AFFINITY group{};
            group.Mask = KAFFINITY(~0);
            group.Group = WORD(i & 1);

            auto handle = get_native_handle(m_threads[i]);
            BOOL r = SetThreadGroupAffinity(handle, &group, nullptr);
        }
#endif

        //if (affinity) {
        //    set_thread_affinity(get_native_handle(m_threads[i]), int(i + 1));
        //}
    }
}

Thread_pool::~Thread_pool() noexcept
{
    m_stop = true;
    m_condition.notify_all();

    for (auto& thread : m_threads) {
        thread.join();
    }

    delete[] m_queues;
}

//int Thread_pool::getHardwareConcurrency() {
//    return int(std::thread::hardware_concurrency());
//}

int Thread_pool::size() const
{
    return int(m_threads.size());
}

void Thread_pool::thread(size_t threadID)
{
    static_cast<void>(threadID);

    auto time0 = high_resolution_clock::now();

    while (!m_stop.load(std::memory_order_relaxed)) {
        if (dequeue_and_process()) {
            // remember the last time we processed a task
            time0 = high_resolution_clock::now();
        } else {
            const auto time1   = high_resolution_clock::now();
            const auto elapsed = time1 - time0;
            if (elapsed >= microseconds(1200)) {
                std::unique_lock<std::mutex> lock{m_queue_mutex};

                m_condition.wait_for(lock, milliseconds(120));
            } else { // if (elapsed >= microseconds(2))
                std::this_thread::yield();
            }
            //else {
            //    pause();
            //}
        }
    }
}

void Thread_pool::enqueue(Queue* queue, std::function<void()>&& func)
{
    Task task;
    task.queue = queue;
    task.func = std::move(func);

    ++queue->task_counter;
    m_queues[queue->priority].tasks.enqueue(std::move(task));

    m_condition.notify_one();
}

bool Thread_pool::dequeue_and_process()
{
    // scan task queues in priority order
    for (size_t priority = 0; priority < 3; ++priority) {
        Task task;
        if (m_queues[priority].tasks.try_dequeue(task)) {
            Queue* const queue = task.queue;

            // check if the task is cancelled
            if (!queue->cancelled) {
                // process task
                task.func();
            }

            --queue->task_counter;
            return true;
        }
    }

    return false;
}

void Thread_pool::wait(Queue* queue)
{
    while (queue->task_counter > 0) {
        dequeue_and_process();
    }
}

void Thread_pool::cancel(Queue* queue)
{
    queue->cancelled = true;
    wait(queue);
    queue->cancelled = false;
}

}
