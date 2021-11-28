#include "erhe/components/components.hpp"
#include "erhe/components/component.hpp"
#include "erhe/components/log.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/tracy_client.hpp"

#include <fmt/ostream.h>
#include <mango/core/thread.hpp>

#include <algorithm>
#include <sstream>

namespace erhe::components
{

using std::set;
using std::shared_ptr;

namespace {

constexpr bool s_parallel_component_initialization{false};

}

auto Components::parallel_component_initialization() -> bool
{
    return s_parallel_component_initialization;
}

auto Components::serial_component_initialization() -> bool
{
    return !parallel_component_initialization();
}

Components::Components()
{
}

Components::~Components() = default;

auto Components::add(const shared_ptr<Component>& component)
-> Component&
{
    VERIFY(component);

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

    return *component.get();
}

void Components::deitialize_component(Component* component)
{
    {
        auto* fixed_step_update = dynamic_cast<IUpdate_fixed_step*>(component);
        if (fixed_step_update != nullptr)
        {
            const auto erase_count = fixed_step_updates.erase(fixed_step_update);
            if (erase_count == 0)
            {
                log_components.error("Component/IUpdate_fixed_step {} not found\n", component->name());
            }
        }
    }

    {
        auto* once_per_frame_update = dynamic_cast<IUpdate_once_per_frame*>(component);
        if (once_per_frame_update != nullptr)
        {
            const auto erase_count = once_per_frame_updates.erase(once_per_frame_update);
            if (erase_count == 0)
            {
                log_components.error("Component/IUpdate_once_per_frame {} not found\n", component->name());
            }
        }
    }

    {
        const auto erase_count = m_components_to_process.erase(component);
        if (erase_count != 1)
        {
            log_components.error("Component {} not found in components to process\n", component->name());
        }
    }

    // This must be last
    {
        const auto erase_count = erase_if(
            components,
            [component](const std::shared_ptr<Component>& shared_component)
            {
                return shared_component.get() == component;
            }
        );
        if (erase_count == 0)
        {
            log_components.error("Component {} not found\n", component->name());
        }
        else if (erase_count != 1)
        {
            log_components.error("Component {} found more than once: {}\n", component->name(), erase_count);
        }
    }
}

void Components::cleanup_components()
{
    ZoneScoped;

    queue_all_components_to_be_processed();

    log_components.info("Deinitializing {} Components:\n", m_components_to_process.size());
    show_depended_by();

    for (;;)
    {
        auto* component = get_component_to_deinitialize();
        if (component == nullptr)
        {
            break;
        }
        log_components.info("Deinitializing {}\n", component->name());
        deitialize_component(component);
        for (auto component_ : m_components_to_process)
        {
            component_->component_deinitialized(component);
        }
    }
    if (components.size() > 0)
    {
        log_components.error("Not all components were deinitialized\n");
        show_depended_by();
        components.clear();
    }
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
            component->processing_requires_main_thread()
                ? "main"
                : "worker"
        );
        for (auto const& dependency : component->dependencies())
        {
            log_components.info(
                "        {} - {}\n",
                dependency->name(),
                dependency->processing_requires_main_thread()
                    ? "main"
                    : "worker"
            );
        }
    }
}

void Components::show_depended_by() const
{
    log_components.info("Component depended by:\n");
    for (auto const& component : components)
    {
        log_components.info(
            "    {}:\n",
            component->name()
        );
        for (auto const& depended_by : component->depended_by())
        {
            log_components.info(
                "        {}\n",
                depended_by->name()
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
        m_components_to_process.erase(component);
        for (auto component_ : m_components_to_process)
        {
            component_->component_initialized(component);
        }
        const std::string message_text = fmt::format("{} initialized", component->name());
        TracyMessage(message_text.c_str(), message_text.length());
        m_component_processed.notify_all();
        if (m_components_to_process.empty())
        {
            m_is_ready = true;
            TracyMessageL("all components initialized");
        }
    }
}

void Components::queue_all_components_to_be_processed()
{
    std::transform(
        components.begin(),
        components.end(),
        std::inserter(m_components_to_process, m_components_to_process.begin()),
        [](const std::shared_ptr<Component>& c) -> Component*
        {
            return c.get();
        }
    );
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
        if (component->processing_requires_main_thread())
        {
            ++m_initialize_component_count_main_thread;
        }
        else
        {
            ++m_initialize_component_count_worker_thread;
        }
    }

    show_dependencies();
    queue_all_components_to_be_processed();

    log_components.info("Initializing {} Components:\n", m_components_to_process.size());

    if (parallel_component_initialization())
    {
        m_execution_queue = std::make_unique<Concurrent_execution_queue>();
    }
    else
    {
        m_execution_queue = std::make_unique<Serial_execution_queue>();
    }

    const bool in_worker_thread = parallel_component_initialization();
    for (size_t i = 0; i < m_initialize_component_count_worker_thread; ++i)
    {
        m_execution_queue->enqueue(
            [this, in_worker_thread]()
            {
                initialize_component(in_worker_thread);
            }
        );
    }
}

auto Components::get_component_to_initialize(const bool in_worker_thread) -> Component*
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_components_to_process.empty())
        {
            FATAL("No uninitialized component found\n");
        }
    }

    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto i = std::find_if(
                m_components_to_process.begin(),
                m_components_to_process.end(),
                [in_worker_thread](auto& component)
                {
                    return component->is_ready_to_initialize(in_worker_thread);
                }
            );
            if (i != m_components_to_process.end())
            {
                auto component = *i;
                component->set_initializing();
                return component;
            }
        }

        if (serial_component_initialization())
        {
            log_components.error("no component to initialize\n");
            abort();
        }

        {
            ZoneScopedNC("wait", 0x444444);

            std::unique_lock<std::mutex> unique_lock(m_mutex);
            m_component_processed.wait(unique_lock);
        }
    }
}

auto Components::get_component_to_deinitialize() -> Component*
{
    const auto i = std::find_if(
        m_components_to_process.begin(),
        m_components_to_process.end(),
        [](auto& component)
        {
            const bool is_ready = component->is_ready_to_deinitialize();
            return is_ready;
        }
    );
    if (i == m_components_to_process.end())
    {
        log_components.error("Unable to find component to deinitialize\n");
        return nullptr;
    }
    auto component = *i;
    component->set_deinitializing();
    return component;
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
