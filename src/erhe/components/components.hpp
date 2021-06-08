#pragma once

#include "mango/core/thread.hpp"

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

class IExecution_queue
{
public:
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

    auto add                                   (const std::shared_ptr<Component>& component) -> const std::shared_ptr<Component>&;
    void show_dependencies                     () const;
    void cleanup_components                    ();
    void launch_component_initialization       ();
    auto get_component_to_initialize           () -> std::shared_ptr<Component>;
    auto is_component_initialization_complete  () -> bool;
    void wait_component_initialization_complete();
    void on_thread_exit                        ();
    void on_thread_enter                       ();

    std::set<std::shared_ptr<Component>> components;
    std::set<IUpdate_fixed_step    *>    fixed_step_updates;
    std::set<IUpdate_once_per_frame*>    once_per_frame_updates;

private:
    std::mutex                           m_mutex;
    bool                                 m_is_ready{false};
    std::condition_variable              m_component_initialized;
    std::set<std::shared_ptr<Component>> m_uninitialized_components;
    std::unique_ptr<IExecution_queue>    m_execution_queue;
};

} // namespace erhe::components
