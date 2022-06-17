#pragma once

#include <erhe/concurrency/concurrent_queue.hpp>
//#include <erhe/concurrency/serial_queue.hpp>

namespace editor {

class Task_queue
{
public:
    void set_parallel(bool parallel);
    void enqueue     (std::function<void()>&& func);
    void wait        ();

private:
    bool                                m_parallel;
    erhe::concurrency::Concurrent_queue m_queue;
};

} // namespace editor
