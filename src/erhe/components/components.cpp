#include "erhe/components/components.hpp"
#include "erhe/components/component.hpp"
#include "erhe/components/log.hpp"
#include "erhe/toolkit/verify.hpp"

#include <mango/core/thread.hpp>

#define ERHE_TRACY_NO_GL 1
#include "erhe/toolkit/tracy_client.hpp"

#include <fmt/ostream.h>

#include <sstream>

namespace erhe::components
{

using std::set;
using std::shared_ptr;

Components::Components()
{
}

Components::~Components() = default;

auto Components::add(const shared_ptr<Component>& component)
-> const std::shared_ptr<erhe::components::Component>&
{
    // Silently ignores nullptr Components
    if (!component)
    {
        return component;
    }

    component->register_as_component(this);
    components.insert(component);

    auto* fixed_step_update     = dynamic_cast<IUpdate_fixed_step    *>(component.get());
    auto* once_per_frame_update = dynamic_cast<IUpdate_once_per_frame*>(component.get());

    if (fixed_step_update != nullptr)
    {
        fixed_step_updates.insert(fixed_step_update);
    }
    if (once_per_frame_update != nullptr)
    {
        once_per_frame_updates.insert(once_per_frame_update);
    }

    return component;
}

void Components::cleanup_components()
{
    ZoneScoped;

    for (const auto& component : components)
    {
        component->unregister();
    }

    components.clear();
}

IExecution_queue::~IExecution_queue() = default;

// Optimized queue which executes tasks concurrently
class Concurrent_execution_queue
    : public IExecution_queue
{
public:
    ~Concurrent_execution_queue() override = default;

    void enqueue(std::function<void()> task) override;
    void wait   () override;

private:
    mango::ConcurrentQueue m_concurrent_queue;
};

void Concurrent_execution_queue::enqueue(std::function<void()> task)
{
    m_concurrent_queue.enqueue(task);
}

void Concurrent_execution_queue::wait()
{
    m_concurrent_queue.wait();
}

// Reference queue which executes taska as they are queued
class Serial_execution_queue
    : public IExecution_queue
{
public:
    ~Serial_execution_queue() override = default;

    void enqueue(std::function<void()> task) override;
    void wait   () override;
};

void Serial_execution_queue::enqueue(std::function<void()> task)
{
    task();
}

void Serial_execution_queue::wait()
{
}

void Components::show_dependencies() const
{
    log_components.info("Component dependencies:\n");
    for (auto const& component : components)
    {
        log_components.info(
            "    {} - {}:\n",
            component->name(),
            component->initialization_requires_main_thread()
                ? "main"
                : "worker"
        );
        for (auto const& dependency : component->dependencies())
        {
            log_components.info(
                "        {} - {}\n",
                dependency->name(),
                dependency->initialization_requires_main_thread()
                    ? "main"
                    : "worker"
            );
        }
    }
}

void Components::initialize_component(const bool in_worker_thread)
{
    ZoneScoped;

    auto component = get_component_to_initialize(in_worker_thread);
    if (!component)
    {
        return;
    }

    log_components.info(
        "Initializing {} {}\n",
        component->name(),
        in_worker_thread
            ? "in worker thread"
            : "in main thread"
    );
    component->initialize_component();

    {
        ZoneName(component->name().data(), component->name().length());

        std::lock_guard<std::mutex> lock(m_mutex);
        component->set_ready();
        m_uninitialized_components.erase(component);
        for (auto component_ : m_uninitialized_components)
        {
            component_->remove_dependency(component);
        }
        const std::string message_text = fmt::format("{} initialized", component->name());
        TracyMessage(message_text.c_str(), message_text.length());
        m_component_initialized.notify_all();
        if (m_uninitialized_components.empty())
        {
            m_is_ready = true;
            TracyMessageL("all components initialized");
        }
    }
}

void Components::launch_component_initialization()
{
    ZoneScoped;

    m_initialize_component_count_worker_thread = 0;
    m_initialize_component_count_main_thread   = 0;
    for (auto const& component : components)
    {
        component->connect();
        component->set_connected();
        if (component->initialization_requires_main_thread())
        {
            ++m_initialize_component_count_main_thread;
        }
        else
        {
            ++m_initialize_component_count_worker_thread;
        }
    }

    show_dependencies();

    m_uninitialized_components = components;

    log_components.info("Initializing {} Components:\n", m_initialize_component_count_worker_thread);

    if constexpr (s_parallel_component_initialization)
    {
        m_execution_queue = std::make_unique<Concurrent_execution_queue>();
    }
    else
    {
        m_execution_queue = std::make_unique<Serial_execution_queue>();
    }

    constexpr bool in_worker_thread = s_parallel_component_initialization;

    for (size_t i = 0; i < m_initialize_component_count_worker_thread; ++i)
    {
        m_execution_queue->enqueue(
            [this]()
            {
                initialize_component(in_worker_thread);
            }
        );
    }
}

auto Components::get_component_to_initialize(const bool in_worker_thread) -> shared_ptr<Component>
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_uninitialized_components.empty())
        {
            FATAL("No uninitialized component found\n");
        }
    }

    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto i = std::find_if(
                m_uninitialized_components.begin(),
                m_uninitialized_components.end(),
                [in_worker_thread](auto& component)
                {
                    return
                        component->get_state() == Component::Component_state::Connected &&
                        component->is_ready_to_initialize(in_worker_thread);
                }
            );
            if (i != m_uninitialized_components.end())
            {
                auto component = *i;
                component->set_initializing();
                return component;
            }
        }

        {
            ZoneScopedNC("wait", 0x444444);

            std::unique_lock<std::mutex> unique_lock(m_mutex);
            m_component_initialized.wait(unique_lock);
        }
    }
}

auto Components::is_component_initialization_complete() -> bool
{
    ZoneScoped;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_is_ready;
}

void Components::wait_component_initialization_complete()
{
    ZoneScoped;

    VERIFY(m_execution_queue);

    // Initialize main thread components
    constexpr bool in_worker_thread{false};
    for (size_t i = 0; i < m_initialize_component_count_main_thread; ++i)
    {
        initialize_component(in_worker_thread);
    }

    m_execution_queue->wait();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_is_ready = true;
}

void Components::on_thread_exit()
{
    ZoneScoped;

    for (const auto& component : components)
    {
        component->on_thread_exit();
    }
}

void Components::on_thread_enter()
{
    ZoneScoped;

    for (const auto& component : components)
    {
        component->on_thread_enter();
    }
}

} // namespace erhe::components
