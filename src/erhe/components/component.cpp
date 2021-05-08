#include "erhe/components/component.hpp"

namespace erhe::components
{

Component::Component(const char* name)
    : m_name{name}
{
}

void Component::initialization_depends_on(const std::shared_ptr<Component>& a)
{
    if (a)
    {
        if (!a->is_registered())
        {
            log_components.error("Component {} dependency {} has not been registered as a Component",
                                    name(),
                                    a->name());
            FATAL("Dependency has not been registered");
        }
        m_dependencies.insert(a);
    }
}

auto Component::is_initialized() const
-> bool
{
    return m_initialized;
}

void Component::initialize()
{
    initialize_component();
    m_initialized = true;
}

auto Component::ready() const
-> bool
{
    return m_dependencies.empty();
}

void Component::remove_dependencies(const std::set<std::shared_ptr<Component>>& remove_set)
{
    for (const auto& dependency : remove_set)
    {
        m_dependencies.erase(dependency);
    }
}

} // namespace erhe::components
