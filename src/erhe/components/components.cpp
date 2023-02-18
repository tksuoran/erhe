#include <fmt/core.h>
#include <fmt/ostream.h>

#include "erhe/components/components.hpp"
#include "erhe/concurrency/concurrent_queue.hpp"
#include "erhe/components/components_log.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

#include <algorithm>
#include <sstream>

namespace erhe::components
{

using std::set;


Components::Components()
{
}

Components::~Components() noexcept
{
}

auto Components::add(Component* component) -> Component&
{
    ERHE_VERIFY(component);

    for (auto& c : m_components) {
        if (c->get_type_hash() == component->get_type_hash()) {
            ERHE_FATAL(
                "Component %s type has collision with %s",
                c->name().data(),
                component->name().data()
            );
        }
    }

    component->register_as_component(this);
    m_components.push_back(component);

    auto* fixed_step_update     = dynamic_cast<IUpdate_fixed_step    *>(component);
    auto* once_per_frame_update = dynamic_cast<IUpdate_once_per_frame*>(component);

    if (fixed_step_update != nullptr) {
        m_fixed_step_updates.insert(fixed_step_update);
    }
    if (once_per_frame_update != nullptr) {
        m_once_per_frame_updates.insert(once_per_frame_update);
    }

    return *component;
}

void Components::deinitialize_component(Component* component)
{
    ERHE_VERIFY(component != nullptr);

    {
        auto* fixed_step_update = dynamic_cast<IUpdate_fixed_step*>(component);
        if (fixed_step_update != nullptr) {
            const auto erase_count = m_fixed_step_updates.erase(fixed_step_update);
            if (erase_count == 0) {
                log_components->error(
                    "Component/IUpdate_fixed_step {} not found",
                    component->name()
                );
            }
        }
    }

    {
        auto* once_per_frame_update = dynamic_cast<IUpdate_once_per_frame*>(component);
        if (once_per_frame_update != nullptr) {
            const auto erase_count = m_once_per_frame_updates.erase(once_per_frame_update);
            if (erase_count == 0) {
                log_components->error(
                    "Component/IUpdate_once_per_frame {} not found",
                    component->name()
                );
            }
        }
    }

    {
        const auto erase_count = m_components_to_process.erase(component);
        if (erase_count != 1) {
            log_components->error(
                "Component {} not found in components to process",
                component->name()
            );
        }
    }

    component->deinitialize_component();

    // This must be last
    {
        const auto erase_it = std::remove_if(
            m_components.begin(),
            m_components.end(),
            [component](Component* entry)
            {
                return entry == component;
            }
        );
        const auto erase_count = std::distance(erase_it, m_components.end());
        m_components.erase(erase_it, m_components.end());
        if (erase_count == 0) {
            log_components->error(
                "Component {} not found",
                component->name()
            );
        } else if (erase_count != 1) {
            log_components->error(
                "Component {} found more than once: {}",
                component->name(),
                erase_count
            );
        }
    }
}

void Components::post_initialize_components()
{
    ERHE_PROFILE_FUNCTION

    log_components->info(
        "Post-initializing {} Components:",
        m_components.size()
    );

    for (const auto& component : m_components) {
        ERHE_VERIFY(component->get_state() == Component_state::Initialized);
        log_components->info("Post initializing {}", component->name());
        component->set_state(Component_state::Post_initializing);
        component->post_initialize();
        component->set_state(Component_state::Ready);
    }
}

void Components::cleanup_components()
{
    ERHE_PROFILE_FUNCTION

    queue_all_components_to_be_processed();

    log_components->info(
        "Deinitializing {} Components:",
        m_components_to_process.size()
    );

    for (auto i = m_initialization_order.rbegin(), end = m_initialization_order.rend(); i != end; ++i) {
        auto* component = *i; //get_component_to_deinitialize();
        log_components->info("Deinitializing {}", component->name());
        component->set_state(Component_state::Deinitializing);
        deinitialize_component(component);
        component->set_state(Component_state::Deinitialized);
    }
    if (m_components.size() > 0) {
        log_components->error("Not all components were deinitialized");
        m_components.clear();
    }
}

IExecution_queue::~IExecution_queue() noexcept
{
}

// Optimized queue which executes tasks concurrently
class Concurrent_execution_queue
    : public IExecution_queue
{
public:
    explicit Concurrent_execution_queue(std::size_t thread_count);

    void enqueue(std::function<void()> task) override;
    void wait   () override;

private:
    erhe::concurrency::Thread_pool      m_thread_pool;
    erhe::concurrency::Concurrent_queue m_concurrent_queue;
};

Concurrent_execution_queue::Concurrent_execution_queue(size_t thread_count)
    : m_thread_pool     {thread_count}
    , m_concurrent_queue{m_thread_pool, "component initialization"}
{
}

void Concurrent_execution_queue::enqueue(std::function<void()> task)
{
    m_concurrent_queue.enqueue(task);
}

void Concurrent_execution_queue::wait()
{
    m_concurrent_queue.wait();
}

// Reference queue which executes tasks as they are queued
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
    ERHE_PROFILE_FUNCTION

    log_components->info("Component dependencies:");
    for (auto const& component : m_components) {
        log_components->info(
            "    {} - {}:",
            component->name(),
            component->processing_requires_main_thread()
                ? "main"
                : "worker"
        );
        for (auto const& dependency : component->dependencies()) {
            log_components->info(
                "        {} - {}",
                dependency->name(),
                dependency->processing_requires_main_thread()
                    ? "main"
                    : "worker"
            );
        }
    }
}

void Components::collect_uninitialized_depended_by(Component* component, std::set<Component*>& result)
{
    ERHE_PROFILE_FUNCTION

    for (Component* dependend_by_component : component->get_depended_by()) {
        if (dependend_by_component->get_state() == Component_state::Initialized) {
            continue;
        }
        result.insert(dependend_by_component);
        collect_uninitialized_depended_by(dependend_by_component, result);
    }
}

void Components::initialize_component(const bool in_worker_thread)
{
    ERHE_PROFILE_SCOPE("initialize_component");

    Component* component;
    {
        component = get_component_to_initialize(in_worker_thread);
        if (!component) {
            return;
        }

        const std::string message_text = fmt::format(
            "Initializing {} {}",
            component->name(),
            in_worker_thread
                ? "in worker thread"
                : "in main thread"
        );
        ERHE_PROFILE_MESSAGE(message_text.c_str(), message_text.length());
        log_components->trace(message_text);
    }

    log_components->info(
        "Initializing {} {}",
        component->name(),
        in_worker_thread
            ? "in worker thread"
            : "in main thread"
    );
    component->initialize_component();
    component->set_state(Component_state::Initialized);

    {
        //ERHE_PROFILE_DATA("init component", component->name().data(), component->name().length())

        const std::lock_guard<std::mutex> lock{m_mutex};

        m_components_to_process.erase(component);

        //// log_components->trace("remaining components to process:");
        //// for (auto component_ : m_components_to_process) {
        ////     log_components->trace("    {}", component_->name());
        //// }

        m_components_to_process.erase(component);
        for (auto component_ : m_components_to_process) {
            component_->component_initialized(component);
        }
        ++m_count_initialized_in_worker_thread;
        const std::string message_text = fmt::format("{} initialized (total {})", component->name(), m_count_initialized_in_worker_thread);
        ERHE_PROFILE_MESSAGE(message_text.c_str(), message_text.length());
        log_components->trace(message_text);
        m_component_processed.notify_all();
        if (m_components_to_process.empty())
        {
            m_is_ready = true;
            ERHE_PROFILE_MESSAGE_LITERAL("all components initialized");
        }

    }
}

void Components::queue_all_components_to_be_processed()
{
    ERHE_PROFILE_FUNCTION

    std::transform(
        m_components.begin(),
        m_components.end(),
        std::inserter(m_components_to_process, m_components_to_process.begin()),
        [](Component* c) -> Component*
        {
            return c;
        }
    );
}

void Components::launch_component_initialization(const bool parallel)
{
    ERHE_PROFILE_FUNCTION

    m_parallel_initialization                  = parallel;
    m_initialize_component_count_worker_thread = 0;
    m_initialize_component_count_main_thread   = 0;

    {
        ERHE_PROFILE_SCOPE("declare_required_components");

        for (auto const& component : m_components) {
            component->set_state(Component_state::Declaring_initialization_requirements);
            component->declare_required_components();
            component->set_state(Component_state::Initialization_requirements_declared);
            if (component->get_state() == Component_state::Ready) {
                continue;
            }
            if (
                !parallel ||
                component->processing_requires_main_thread()
            ) {
                ++m_initialize_component_count_main_thread;
            } else {
                ++m_initialize_component_count_worker_thread;
            }
        }
    }

    show_dependencies();
    queue_all_components_to_be_processed();

    log_components->info(
        "Initializing {} components - {} in worker thread, {} in main thread:",
        m_components_to_process.size(),
        m_initialize_component_count_worker_thread,
        m_initialize_component_count_main_thread
    );

    if (parallel) {
        std::size_t thread_count = std::min(
            8U,
            std::max(std::thread::hardware_concurrency() - 0, 1U)
        );
        m_execution_queue = std::make_unique<Concurrent_execution_queue>(thread_count);
    } else {
        m_execution_queue = std::make_unique<Serial_execution_queue>();
    }

    {
        ERHE_PROFILE_SCOPE("enqueue");

        const bool in_worker_thread = parallel;
        for (size_t i = 0; i < m_initialize_component_count_worker_thread; ++i) {
            m_execution_queue->enqueue(
                [this, in_worker_thread]()
                {
                    initialize_component(in_worker_thread);
                }
            );
        }
    }
}

auto Components::get_component_to_initialize(const bool in_worker_thread) -> Component*
{
    ERHE_PROFILE_FUNCTION

    {
        const std::lock_guard<std::mutex> lock{m_mutex};

        if (m_components_to_process.empty()) {
            ERHE_FATAL("No uninitialized component found");
        }
    }

    for (;;) {
        {
            const std::lock_guard<std::mutex> lock{m_mutex};

            std::size_t most_uninitialized_depended_by = 0;
            Component*  selected_component{nullptr};

            for (Component* component : m_components_to_process) {
                if (!component->is_ready_to_initialize(in_worker_thread, m_parallel_initialization)) {
                    continue;
                }
                std::set<Component*> depended_by;
                collect_uninitialized_depended_by(component, depended_by);
                if (
                    (selected_component == nullptr) ||
                    (depended_by.size() > most_uninitialized_depended_by)
                ) {
                    most_uninitialized_depended_by = depended_by.size();
                    selected_component = component;
                }
            }
            if (selected_component != nullptr) {
                log_components->trace(
                    "selected component {} for {}",
                    selected_component->name(),
                    in_worker_thread
                        ? "worker thread"
                        : "main thread"
                );
                selected_component->set_state(Component_state::Initializing);
                m_initialization_order.push_back(selected_component);
                return selected_component;
            }
        }

        if (!m_parallel_initialization) {
            log_components->error("Possible cyclic component dependency. Component initialization cannot make progress, no component is ready to be initialized.");
            abort();
        }

        {
            ERHE_PROFILE_COLOR("wait", 0x444444);

            std::unique_lock<std::mutex> unique_lock(m_mutex);
            m_component_processed.wait(unique_lock);
        }
    }
}

//auto Components::get_component_to_deinitialize() -> Component*
//{
//    const auto i = std::find_if(
//        m_components_to_process.begin(),
//        m_components_to_process.end(),
//        [](auto& component)
//        {
//            const bool is_ready = component->is_ready_to_deinitialize();
//            return is_ready;
//        }
//    );
//    if (i == m_components_to_process.end()) {
//        log_components->error("Unable to find component to deinitialize");
//        return nullptr;
//    }
//    auto component = *i;
//    component->set_state(Component_state::Deinitializing);
//    return component;
//}

auto Components::is_component_initialization_complete() -> bool
{
    ERHE_PROFILE_FUNCTION

    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_is_ready; // TODO atomic bool
}

void Components::wait_component_initialization_complete()
{
    ERHE_PROFILE_FUNCTION

    ERHE_VERIFY(m_execution_queue);

    // Initialize main thread components
    constexpr bool in_worker_thread{false};
    for (size_t i = 0; i < m_initialize_component_count_main_thread; ++i) {
        initialize_component(in_worker_thread);
    }

    m_execution_queue->wait();
    m_execution_queue.reset();

    post_initialize_components();

    const std::lock_guard<std::mutex> lock{m_mutex};
    m_is_ready = true;

}

void Components::update_fixed_step(
    const Time_context& time_context
)
{
    ERHE_PROFILE_FUNCTION

    for (auto update : m_fixed_step_updates) {
        update->update_fixed_step(time_context);
    }
}

void Components::update_once_per_frame(
    const Time_context& time_context
)
{
    ERHE_PROFILE_FUNCTION

    for (auto update : m_once_per_frame_updates) {
        update->update_once_per_frame(time_context);
    }
}

void Components::on_thread_exit()
{
    ERHE_PROFILE_FUNCTION

    for (const auto& component : m_components) {
        component->on_thread_exit();
    }
}

void Components::on_thread_enter()
{
    ERHE_PROFILE_FUNCTION

    for (const auto& component : m_components) {
        component->on_thread_enter();
    }
}

} // namespace erhe::components

