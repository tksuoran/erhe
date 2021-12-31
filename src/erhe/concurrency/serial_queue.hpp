#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace erhe::concurrency {

// ----------------------------------------------------------------------------------
// SerialQueue
// ----------------------------------------------------------------------------------

/*
    SerialQueue is API to serialize tasks to be executed after previous task
    in the queue has completed. The tasks are NOT executed in the Thread_pool; each
    queue has it's own execution thread.

    SerialQueue and ConcurrentQueue can be freely mixed can can enqueue work to other
    queues from their tasks.

    Usage example:

    // create queue
    SerialQueue s;

    // submit work into the queue
    s.enqueue([]
    {
        // TODO: do your stuff here..
    });

    // wait until the queue is drained
    s.wait(); // non-cooperative, blocking (CPU sleeps until queue is drained)

*/

class Serial_queue
{
protected:
    using Task = std::function<void()>;

    std::string m_name;
    std::thread m_thread;

#pragma warning(push)
#pragma warning(disable : 4324)  // structure was padded due to alignment specifier
    alignas(64) std::atomic<bool> m_stop        { false };
    alignas(64) std::atomic<int>  m_task_counter{ 0 };
#pragma warning(pop)

    std::deque<Task>        m_task_queue;
    std::mutex              m_queue_mutex;
    std::mutex              m_wait_mutex;
    std::condition_variable m_task_condition;
    std::condition_variable m_wait_condition;

    void thread();

public:
    Serial_queue();
    explicit Serial_queue(const std::string_view name);
    ~Serial_queue();

    template <class F, class... Args>
    void enqueue(F&& f, Args&&... args)
    {
        std::unique_lock<std::mutex> lock{m_queue_mutex};

        m_task_queue.emplace_back(f, (args)...);
        ++m_task_counter;
        m_task_condition.notify_one();
    }

    void cancel();
    void wait  ();
};

}