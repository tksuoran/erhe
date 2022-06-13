/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <queue>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <future>
#include <mango/core/exception.hpp>
#include <mango/core/memory.hpp>
#include <mango/core/atomic.hpp>

namespace mango
{

    // ----------------------------------------------------------------------------------
    // ThreadPool
    // ----------------------------------------------------------------------------------

    class ThreadPool : private NonCopyable
    {
    private:
        friend class ConcurrentQueue;

        struct Queue
        {
            ThreadPool* pool;
            int priority;
            std::string name;

            alignas(64) std::atomic<int> task_counter { 0 };
            alignas(64) std::atomic<bool> cancelled { false };

            Queue(ThreadPool* pool, int priority, const std::string& name)
                : pool(pool)
                , priority(priority)
                , name(name)
            {
            }
        };

        struct Task
        {
            Queue* queue;
            std::function<void()> func;
        };

    public:
        ThreadPool(size_t size);
        ~ThreadPool();

        static int getHardwareConcurrency();
        static ThreadPool& getInstance();

        int size() const;

        void enqueue(std::function<void()>&& func)
        {
            enqueue(&m_static_queue, std::move(func));
        }

    protected:
        void thread(size_t threadID);

        void enqueue(Queue* queue, std::function<void()>&& func);
        bool dequeue_and_process();
        void cancel(Queue* queue);
        void wait(Queue* queue);

    private:
        static ThreadPool m_static_instance;

        struct TaskQueue;
        alignas(64) TaskQueue* m_queues;

        alignas(64) std::atomic<bool> m_stop { false };
        std::mutex m_queue_mutex;
        std::condition_variable m_condition;

        Queue m_static_queue;
        std::vector<std::thread> m_threads;
    };

    enum class Priority
    {
        HIGH = 0,
        NORMAL = 1,
        LOW = 2
    };

    // ----------------------------------------------------------------------------------
    // ConcurrentQueue
    // ----------------------------------------------------------------------------------

    /*
        ConcurrentQueue is API to submit work into the ThreadPool. The tasks have no
        dependency to each other and can be executed in any order. Any number of queues
        can be created from any thread in the program. The ThreadPool is shared between
        queues. The queues can be configuted to different priorities to control which tasks
        are more time critical.

        Usage example:

        // create queue
        ConcurrentQueue q;

        // submit work into the queue
        q.enqueue([]
        {
            // TODO: do your stuff here..
        });

        // wait until the queue is drained
        q.wait(); // cooperative, blocking (helps pool until all tasks are complete)

    */

    class ConcurrentQueue : private NonCopyable
    {
    protected:
        ThreadPool& m_pool;
        ThreadPool::Queue m_queue;

    public:
        ConcurrentQueue();
        ConcurrentQueue(const std::string& name, Priority priority = Priority::NORMAL);
        ~ConcurrentQueue();

        template <class F, class... Args>
        void enqueue(F&& f, Args&&... args)
        {
            m_pool.enqueue(&m_queue, std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        }

        void steal();
        void cancel();
        void wait();
    };

    // ----------------------------------------------------------------------------------
    // SerialQueue
    // ----------------------------------------------------------------------------------

    /*
        SerialQueue is API to serialize tasks to be executed after previous task
        in the queue has completed. The tasks are NOT executed in the ThreadPool; each
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

    class SerialQueue : private NonCopyable
    {
    protected:
        using Task = std::function<void()>;

        std::string m_name;
        std::thread m_thread;

        alignas(64) std::atomic<bool> m_stop { false };
        alignas(64) std::atomic<int> m_task_counter { 0 };

        std::deque<Task> m_task_queue;
        std::mutex m_queue_mutex;
        std::mutex m_wait_mutex;
        std::condition_variable m_task_condition;
        std::condition_variable m_wait_condition;

        void thread();

    public:
        SerialQueue();
        SerialQueue(const std::string& name);
        ~SerialQueue();

        template <class F, class... Args>
        void enqueue(F&& f, Args&&... args)
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            m_task_queue.emplace_back(f, (args)...);
            ++m_task_counter;
            m_task_condition.notify_one();
        }

        void cancel();
        void wait();
    };

    // ----------------------------------------------------------------------------------
    // TicketQueue
    // ----------------------------------------------------------------------------------

    /*
        TicketQueue is a non-blocking serialization mechanism which allows to schedule work
        from ANY thread with deterministic order. The order is determined by a ticket based
        system; tickets are consumed in the same order they are acquired.

        Usage example:

        ConcurrentQueue q;
        TicketQueue tk;

        for (int i = 0; i < 10; ++i)
        {
            auto ticket = tk.acquire();

            q.enqueue([ticket]
            {
                // do your heavy calculation here..

                ticket.consume([]
                {
                    // this work is serialized and executed in the order the tickets were acquired
                });
            });
        }

        // wait until the queue is drained
        q.wait();

        // we know all workers have completed -> no more tickets will be consumed
        // now we wait until ticket queue is finished
        tk.wait();

    */

    class TicketQueue : private NonCopyable
    {
    protected:
        struct Task
        {
            std::atomic<int> count { 1 };
            std::function<void()> func;
            std::promise<void> promise;
        };

    public:
        using SharedTask = std::shared_ptr<Task>;

        class Ticket
        {
        protected:
            friend class TicketQueue;

            SharedTask task;

        public:
            Ticket();
            ~Ticket();

            Ticket(const Ticket& ticket);
            const Ticket& operator = (const Ticket& ticket);

            template <class F, class... Args>
            void consume(F&& f, Args&&... args) const
            {
                task->func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
                task->promise.set_value();
            }
        };

        TicketQueue();
        ~TicketQueue();

        Ticket acquire();
        void wait();

    protected:
        std::thread m_thread;

        alignas(64) std::atomic<bool> m_stop { false };
        alignas(64) std::atomic<int> m_ticket_counter { 0 };

        std::mutex m_wait_mutex;
        std::mutex m_consume_mutex;
        std::condition_variable m_wait_condition;
        std::condition_variable m_consume_condition;

        struct TaskQueue;
        alignas(64) TaskQueue* m_queue;

        bool dequeue_and_process();
    };

    // ----------------------------------------------------------------------------------
    // Task
    // ----------------------------------------------------------------------------------

    /*
        Task is a simple queue-less convenience object to enqueue work into the ThreadPool.
        Synchronization must be done manually.
    */

    class Task
    {
    public:
        template <class F, class... Args>
        Task(F&& f, Args&&... args)
        {
            ThreadPool& pool = ThreadPool::getInstance();
            pool.enqueue(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        }
    };

    // ----------------------------------------------------------------------------------
    // FutureTask
    // ----------------------------------------------------------------------------------

    /*
        FutureTask is an asynchronous API to submit tasks into the ThreadPool.
        The get() member function will block the current thread until the result is available
        and does not consume any significant amount of CPU; the thread will yield/sleep
        while waiting for the result.

        Usage example:

        // enqueue a simple task into the ThreadPool
        FutureTask<int> task([] () -> int {
            return 7;
        });

        // this will block until the task has been completed
        int x = task.get();

    */

    template <typename T>
    class FutureTask
    {
    private:
        using Future = std::future<T>;
        using Promise = std::promise<T>;
        using Function = std::function<T()>;

        Promise m_promise;
        Future m_future;

    public:
        template <class F, class... Args>
        FutureTask(F&& f, Args&&... args)
            : m_promise()
            , m_future(m_promise.get_future())
        {
            Function func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            auto container = std::bind([this] (Function func) {
                T value = func();
                m_promise.set_value(value);
            }, func);

            ThreadPool& pool = ThreadPool::getInstance();
            pool.enqueue(container);
        }

        T get()
        {
            return m_future.get();
        }

        void wait()
        {
            m_future.wait();
        }
    };

    template <>
    class FutureTask<void>
    {
    private:
        using Future = std::future<void>;
        using Promise = std::promise<void>;
        using Function = std::function<void()>;

        Promise m_promise;
        Future m_future;

    public:
        template <class F, class... Args>
        FutureTask(F&& f, Args&&... args)
            : m_promise()
            , m_future(m_promise.get_future())
        {
            Function func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            auto container = std::bind([this] (Function func) {
                func();
                m_promise.set_value();
            }, func);

            ThreadPool& pool = ThreadPool::getInstance();
            pool.enqueue(container);
        }

        void get()
        {
            m_future.get();
        }

        void wait()
        {
            m_future.wait();
        }
    };

} // namespace mango
