#include "task_queue.hpp"

namespace editor {

ITask_queue::~ITask_queue()
{
}

Parallel_task_queue::Parallel_task_queue(const std::string_view name, size_t thread_count)
    : m_thread_pool{thread_count}
    , m_queue      {m_thread_pool, name}
{
}

void Serial_task_queue::enqueue(std::function<void()>&& func)
{
    func();
}

void Serial_task_queue::wait()
{
}

void Parallel_task_queue::enqueue(std::function<void()>&& func)
{
    m_queue.enqueue(func);
}

void Parallel_task_queue::wait()
{
    m_queue.wait();
}

}
