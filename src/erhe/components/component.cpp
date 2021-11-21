#include "erhe/components/component.hpp"
#include "erhe/components/log.hpp"
#include "erhe/toolkit/verify.hpp"

#define ERHE_TRACY_NO_GL 1
#include "erhe/toolkit/tracy_client.hpp"

namespace erhe::components
{

Component::Component(const std::string_view name)
    : m_name{name}
{
    ZoneScoped;
}

void Component::initialization_depends_on(Component* dependency)
{
    ZoneScoped;

    if (dependency == nullptr)
    {
        return;
    }

    if (!dependency->is_registered())
    {
        log_components.error(
            "Component {} dependency {} has not been registered as a Component",
            name(),
            dependency->name()
        );
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

auto Component::is_ready_to_initialize(const bool in_worker_thread) const
-> bool
{
    return
        m_dependencies.empty() &&
        (
            in_worker_thread != initialization_requires_main_thread()
        );
}

void Component::remove_dependency(Component* dependency)
{
    if (dependency == nullptr)
    {
        return;
    }

    m_dependencies.erase(dependency);
}

} // namespace erhe::components
