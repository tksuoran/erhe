#include "erhe/components/component.hpp"
#include "erhe/components/log.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

namespace erhe::components
{

auto c_str(const Component_state state) -> const char*
{
    switch (state)
    {
        using enum Component_state;
        case Constructed:    return "Constructed";
        case Connected:      return "Connected";
        case Initializing:   return "Initializing";
        case Ready:          return "Ready";
        case Deinitializing: return "Deinitializing";
        case Deinitialized:  return "Deinitialized";
        default: return "?";
    }
}

Component::Component(const std::string_view name)
    : m_name{name}
{
    ERHE_PROFILE_FUNCTION
}

Component::~Component()
{
    unregister();
}

auto Component::processing_requires_main_thread() const -> bool
{
    return false;
}

auto Component::name() const -> std::string_view
{
    return m_name;
}

auto Component::is_registered() const -> bool
{
    return m_components != nullptr;
}

void Component::register_as_component(Components* components)
{
    m_components = components;
}

void Component::unregister()
{
    m_components = nullptr;
}

auto Component::dependencies() -> const std::set<std::shared_ptr<Component>>&
{
    return m_dependencies;
}

void Component::depends_on(const std::shared_ptr<Component>& dependency)
{
    ERHE_VERIFY(dependency);

    if (!dependency->is_registered())
    {
        log_components.error(
            "Component {} dependency {} has not been registered as a Component\n",
            name(),
            dependency->name()
        );
        ERHE_FATAL("Dependency has not been registered");
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

void Component::set_deinitializing()
{
    m_state = Component_state::Initializing;
}

auto Component::get_state() const -> Component_state
{
    return m_state;
}

auto Component::is_ready_to_initialize(
    const bool in_worker_thread
) const -> bool
{
    if (m_state != Component_state::Connected)
    {
        log_components.trace(
            "{} is not ready to initialize: state {} is not Connected\n",
            name(),
            c_str(m_state)
        );
        return false;
    }

    const bool is_ready =
        m_dependencies.empty() &&
        (
            Components::serial_component_initialization() ||
            (in_worker_thread != processing_requires_main_thread())
        );
    if (!is_ready)
    {
        log_components.trace(
            "{} is not ready: requires main thread = {}, dependencies:\n",
            name(),
            processing_requires_main_thread()
        );
        for (const auto& component : m_dependencies)
        {
            log_components.trace(
                "    {}: {}\n",
                component->name(),
                c_str(component->get_state())
            );
        }
    }
    return is_ready;
}

auto Component::is_ready_to_deinitialize() const -> bool
{
    if (m_state != Component_state::Ready)
    {
        log_components.trace(
            "{} is not ready to initialize: state {} is not Ready\n",
            name(),
            c_str(m_state)
        );
        return false;
    }

    return true;
}

void Component::component_initialized(Component* component)
{
    std::erase_if(
        m_dependencies,
        [component](const auto& entry)
        {
            return entry.get() == component;
        }
    );
}

} // namespace erhe::components
