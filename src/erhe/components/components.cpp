#include "erhe/components/components.hpp"
#include "erhe/components/component.hpp"
#include "erhe/components/log.hpp"
#include "erhe/toolkit/verify.hpp"
#include <sstream>

namespace erhe::components
{

using std::set;
using std::shared_ptr;

auto Components::add(const shared_ptr<Component>& component)
-> const std::shared_ptr<erhe::components::Component>&
{
    // Silently ignores nullptr Components
    if (component)
    {
        component->register_as_component(this);
        components.insert(component);
    }
    return component;
}

void Components::cleanup_components()
{
    for (const auto& component : components)
    {
        component->unregister();
    }

    components.clear();
}

void Components::initialize_components()
{
    log_components.info("Connecting {} Components:\n", components.size());
    for (auto const& component : components)
    {
        component->connect();
    }

    set<shared_ptr<Component>> uninitialized(components);
    set<shared_ptr<Component>> remove_set;

    size_t total_count         = uninitialized.size();
    size_t uninitialized_count = total_count;
    size_t initialized_count   = 0;

    log_components.info("Initializing {} Components:\n", uninitialized_count);

    while (uninitialized_count > 0)
    {
        remove_set.clear();

        for (auto i = uninitialized.begin(); i != uninitialized.end();)
        {
            auto s = *i;

            VERIFY(s != nullptr);

            if (s->ready())
            {
                ++initialized_count;
                log_components.info("Initializing Component {} / {}: {}...\n", initialized_count, total_count, s->name());
                s->initialize();
                remove_set.insert(s);
                --uninitialized_count;
                i = uninitialized.erase(i);
            }
            else
            {
                ++i;
            }
        }

        if (remove_set.empty())
        {
            log_components.error("Circular Component dependenciers detected\n");
            for (const auto& s : uninitialized)
            {
                log_components.error("\t{}", s->name());
                for (const auto& d : s->dependencies())
                {
                    log_components.error("\t\t{}", d->name());
                }
            }
            FATAL("Circular dependencies detected");
        }

        for (const auto& s : uninitialized)
        {
            s->remove_dependencies(remove_set);
        }
    }
    log_components.info("Done initializing Components\n");
}

void Components::on_thread_exit()
{
    for (const auto& component : components)
    {
        component->on_thread_exit();
    }
}

void Components::on_thread_enter()
{
    for (const auto& component : components)
    {
        component->on_thread_enter();
    }
}

} // namespace erhe::components
