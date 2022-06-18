#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace erhe::concurrency {

class Thread_pool
{
private:
    Thread_pool(const Thread_pool&) = delete;
    auto operator=(const Thread_pool&) -> Thread_pool = delete;

    friend class Concurrent_queue;

    struct Queue
    {
        Thread_pool* pool;
        int          priority;
        std::string  name;

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4324)  // structure was padded due to alignment specifier
#endif
        alignas(64) std::atomic<int>  task_counter{ 0 };
        alignas(64) std::atomic<bool> cancelled   { false };
#if defined(_MSC_VER)
#   pragma warning(pop)
#endif

        Queue(
            Thread_pool*           pool,
            const int              priority,
            const std::string_view name
        )
            : pool    {pool}
            , priority{priority}
            , name    {name}
        {
        }
    };

    struct Task
    {
        Queue*                queue;
        std::function<void()> func;
    };

public:
    explicit Thread_pool(size_t size);
    ~Thread_pool() noexcept;

    int size() const;

    void enqueue(std::function<void()>&& func)
    {
        enqueue(&m_static_queue, std::move(func));
    }

protected:
    void thread             (size_t threadID);
    void enqueue            (Queue* queue, std::function<void()>&& func);
    bool dequeue_and_process();
    void cancel             (Queue* queue);
    void wait               (Queue* queue);

private:
    struct Task_queue;
    alignas(64) Task_queue* m_queues;

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4324)  // structure was padded due to alignment specifier
#endif
    alignas(64) std::atomic<bool> m_stop { false };
#if defined(_MSC_VER)
#   pragma warning(pop)
#endif

    std::mutex               m_queue_mutex;
    std::condition_variable  m_condition;
    Queue                    m_static_queue;
    std::vector<std::thread> m_threads;
};

enum class Priority
{
    HIGH   = 0,
    NORMAL = 1,
    LOW    = 2
};

}
