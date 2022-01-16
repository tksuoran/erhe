#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <set>

namespace mango {

class ConcurrentQueue;

}

namespace erhe::components
{

/// Components is a collection of Components.
/// Typically you have only one instance of Components in your application.
///
/// Usage:
///  * In each Component connect(), declare dependencies to other Components
///    with require<T>()
///  * Register all components, in any order, with Components::add()
///  * Once all components have been registered, call Components::initialize_components().
///    This will call Component::initialize() for each component, in order which respects
///    all declared dependencies. If there are circular dependencies, initialize_components()
///    will abort.
class Component;
class IUpdate_fixed_step;
class IUpdate_once_per_frame;
class Time_context;

class IExecution_queue
{
public:
    virtual ~IExecution_queue();
    virtual void enqueue(std::function<void()>) = 0;
    virtual void wait   () = 0;
};

class Components final
{
public:
    Components    ();
    ~Components   ();
    Components    (const Components&) = delete;
    void operator=(const Components&) = delete;
    Components    (Components&&)      = delete;
    void operator=(Components&&)      = delete;

    [[nodiscard]] auto is_component_initialization_complete() -> bool;
    [[nodiscard]] auto get_component_to_deinitialize       () -> Component*;

    auto add                                   (const std::shared_ptr<Component>& component) -> Component&;
    void show_dependencies                     () const;
    void cleanup_components                    ();
    void launch_component_initialization       ();
    void wait_component_initialization_complete();
    void on_thread_exit                        ();
    void on_thread_enter                       ();
    void update_fixed_step                     (const Time_context& time_context);
    void update_once_per_frame                 (const Time_context& time_context);

    template<typename T>
    [[nodiscard]] auto get() const -> std::shared_ptr<T>
    {
        for (const auto& component : m_components)
        {
            if (component->get_type_hash() == T::hash)
            {
                return dynamic_pointer_cast<T>(component);
            }
        }
        return {};
    }

    static auto parallel_component_initialization() -> bool;
    static auto serial_component_initialization  () -> bool;

private:
    [[nodiscard]] auto get_component_to_initialize(const bool in_worker_thread) -> Component*;
    void queue_all_components_to_be_processed();
    void initialize_component                (const bool in_worker_thread);
    void deitialize_component                (Component* component);

    std::mutex                           m_mutex;
    std::set<std::shared_ptr<Component>> m_components;
    std::set<IUpdate_fixed_step    *>    m_fixed_step_updates;
    std::set<IUpdate_once_per_frame*>    m_once_per_frame_updates;
    bool                                 m_is_ready{false};
    std::condition_variable              m_component_processed;
    std::set<Component*>                 m_components_to_process;
    std::unique_ptr<IExecution_queue>    m_execution_queue;
    size_t                               m_initialize_component_count_worker_thread{0};
    size_t                               m_initialize_component_count_main_thread  {0};
};

} // namespace erhe::components
