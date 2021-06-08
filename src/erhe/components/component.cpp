#include "erhe/components/component.hpp"
#include "erhe/components/log.hpp"
#include "erhe/toolkit/verify.hpp"

#define ERHE_TRACY_NO_GL 1
#include "erhe/toolkit/tracy_client.hpp"

namespace erhe::components
{

Component::Component(const char* name)
    : m_name{name}
{
    ZoneScoped;
}

void Component::initialization_depends_on(const std::shared_ptr<Component>& dependency)
{
    ZoneScoped;

    if (!dependency)
    {
        return;
    }

    if (!dependency->is_registered())
    {
        log_components.error("Component {} dependency {} has not been registered as a Component",
                             name(),
                             dependency->name());
        FATAL("Dependency has not been registered");
    }
    m_dependencies.insert(dependency);
}

void Component::set_connected()
{
    m_state = Component_state::Connected;
}

void Component::set_initializing()
{
    m_state = Component_state::Initializing;
}

void Component::set_ready()
{
    m_state = Component_state::Ready;
}

auto Component::get_state() const
-> Component_state
{
    return m_state;
}

auto Component::is_ready_to_initialize() const
-> bool
{
    return m_dependencies.empty();
}

void Component::remove_dependency(const std::shared_ptr<Component>& dependency)
{
    m_dependencies.erase(dependency);
}

} // namespace erhe::components
