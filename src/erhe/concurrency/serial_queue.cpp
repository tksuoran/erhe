#include "erhe/concurrency/serial_queue.hpp"

namespace erhe::concurrency {

Serial_queue::Serial_queue()
    : m_name{"serial.default"}
{
    m_thread = std::thread(
        [this]
        {
            thread();
        }
    );
}

Serial_queue::Serial_queue(const std::string_view name)
    : m_name{name}
{
    m_thread = std::thread(
        [this]
        {
            thread();
        }
    );
}

Serial_queue::~Serial_queue() noexcept
{
    wait();

    std::unique_lock<std::mutex> queue_lock{m_queue_mutex};

    m_stop = true;
    queue_lock.unlock();

    m_task_condition.notify_one();
    m_thread.join();
}

void Serial_queue::thread()
{
    while (!m_stop.load(std::memory_order_relaxed)) {
        std::unique_lock<std::mutex> queue_lock{m_queue_mutex};

        if (m_task_counter > 0) {
            auto task = std::move(m_task_queue.front());
            m_task_queue.pop_front();
            queue_lock.unlock();

            task();

            std::unique_lock<std::mutex> wait_lock(m_wait_mutex);
            --m_task_counter;
        } else {
            m_wait_condition.notify_all();
            m_task_condition.wait(
                queue_lock,
                [this]
                {
                    return m_stop || m_task_counter;
                }
            );
        }
    }
}

void Serial_queue::cancel()
{
    std::unique_lock<std::mutex> queue_lock{m_queue_mutex};

    m_task_counter -= static_cast<int>(m_task_queue.size());
    m_task_queue.clear();
}

void Serial_queue::wait()
{
    std::unique_lock<std::mutex> wait_lock{m_wait_mutex};

    m_wait_condition.wait(
        wait_lock,
        [this]
        {
            return !m_task_counter.load(std::memory_order_relaxed);
        }
    );
}

}
