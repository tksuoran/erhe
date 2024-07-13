#include "erhe_concurrency/concurrent_queue.hpp"

namespace erhe::concurrency {

Concurrent_queue::Concurrent_queue(Thread_pool& thread_pool)
    : m_pool {thread_pool}
    , m_queue{&m_pool, int(Priority::NORMAL), "concurrent.default"}
{
}

Concurrent_queue::Concurrent_queue(Thread_pool& thread_pool, const std::string_view name, Priority priority)
    : m_pool {thread_pool}
    , m_queue{&m_pool, static_cast<int>(priority), name}
{
}

Concurrent_queue::~Concurrent_queue() noexcept
{
    wait();
}

void Concurrent_queue::steal()
{
    m_pool.dequeue_and_process();
}

void Concurrent_queue::cancel()
{
    m_pool.cancel(&m_queue);
}

void Concurrent_queue::wait()
{
    m_pool.wait(&m_queue);
}

}
