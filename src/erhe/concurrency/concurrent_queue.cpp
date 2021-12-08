#include "erhe/concurrency/concurrent_queue.hpp"

namespace erhe::concurrency {

Concurrent_queue::Concurrent_queue()
    : m_pool {Thread_pool::get_instance()}
    , m_queue{&m_pool, int(Priority::NORMAL), "concurrent.default"}
{
}

Concurrent_queue::Concurrent_queue(const std::string_view name, Priority priority)
    : m_pool {Thread_pool::get_instance()}
    , m_queue{&m_pool, int(priority), name}
{
}

Concurrent_queue::~Concurrent_queue()
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