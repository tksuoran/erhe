#include "task_queue.hpp"

namespace editor {

void Task_queue::set_parallel(bool parallel)
{
    m_parallel = parallel;
}

void Task_queue::enqueue(std::function<void()>&& func)
{
    if (m_parallel)
    {
        m_queue.enqueue(func);
    }
    else
    {
        func();
    }
}

void Task_queue::wait()
{
    if (m_parallel)
    {
        m_queue.wait();
    }
}

}
