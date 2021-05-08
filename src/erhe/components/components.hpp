#pragma once

//#include "erhe/components/component.hpp"
#include <memory>
#include <set>

namespace erhe::components
{

/// Components is a collection of Components.
/// Typically you have only one instance of Components in your application.
///
/// Usage:
///  * In each Component constructore, declare dependencies to other Components
///    with Component::initialization_depends_on()
///  * Register all components, in any order, with Components::add()
///  * Once all components have been registered, call Components::initialize_components().
///    This will call Component::initialize() for each component, in order which respects
///    all declared dependencies. If there are circular dependencies, initialize_components()
///    will abort.
class Component;

class Components
{
public:
    Components() = default;

    ~Components() = default;

    auto add(const std::shared_ptr<erhe::components::Component>& component)
    -> const std::shared_ptr<erhe::components::Component>&;

    void cleanup_components();

    void initialize_components();

    void on_thread_exit();

    void on_thread_enter();

    std::set<std::shared_ptr<erhe::components::Component>> components;
};

} // namespace erhe::components
