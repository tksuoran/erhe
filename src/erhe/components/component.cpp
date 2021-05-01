#include "erhe/components/component.hpp"

namespace erhe::components
{

Component::Component(std::string name)
    : m_name{std::move(name)}
{
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
