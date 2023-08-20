#pragma once

#include "erhe_concurrency/thread_pool.hpp"

#include <string_view>

namespace erhe::concurrency {

/*
    Concurrent_queue is API to submit work into the Thread_pool. The tasks have no
    dependency to each other and can be executed in any order. Any number of queues
    can be created from any thread in the program. The Thread_pool is shared between
    queues. The queues can be configuted to different priorities to control which tasks
    are more time critical.

    Usage example:

    // create queue
    Concurrent_queue q;

    // submit work into the queue
    q.enqueue([]
    {
        // TODO: do your stuff here..
    });

    // wait until the queue is drained
    q.wait(); // cooperative, blocking (helps pool until all tasks are complete)

*/
class Concurrent_queue
{
protected:
    Thread_pool&       m_pool;
    Thread_pool::Queue m_queue;

public:
    explicit Concurrent_queue(Thread_pool& thread_pool);

    Concurrent_queue(
        Thread_pool&           thread_pool,
        const std::string_view name,
        Priority               priority = Priority::NORMAL
    );
    ~Concurrent_queue() noexcept;

    Concurrent_queue(const Concurrent_queue&) = delete;
    auto operator=(const Concurrent_queue&) -> Concurrent_queue = delete;


    template <class F, class... Args>
    void enqueue(F&& f, Args&&... args)
    {
        m_pool.enqueue(
            &m_queue,
            std::bind(
                std::forward<F>(f),
                std::forward<Args>(args)...
            )
        );
    }

    void steal ();
    void cancel();
    void wait  ();
};

} // namespace erhe::concurrency
