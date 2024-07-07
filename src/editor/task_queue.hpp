#pragma once

#include <erhe_concurrency/concurrent_queue.hpp>

namespace editor {

class ITask_queue
{
public:
    virtual ~ITask_queue();
    virtual void enqueue(std::function<void()>&& func) = 0;
    virtual void wait   () = 0;
};

class Serial_task_queue : public ITask_queue
{
public:
    void enqueue(std::function<void()>&& func) override;
    void wait   () override;
};

class Parallel_task_queue : public ITask_queue
{
public:
    Parallel_task_queue(const std::string_view name, std::size_t thread_count);

    void enqueue(std::function<void()>&& func) override;
    void wait   () override;

private:
    erhe::concurrency::Thread_pool      m_thread_pool;
    erhe::concurrency::Concurrent_queue m_queue;
};

} // namespace editor
