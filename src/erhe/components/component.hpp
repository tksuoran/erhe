#ifndef component_hpp_erhe_components
#define component_hpp_erhe_components

#include "erhe/components/log.hpp"
#include <set>

namespace erhe::components
{

class Component
{
protected:
    Component(const Component& other) = delete;

    Component(Component&& other) = delete;

    auto operator=(const Component&)
    -> Component& = delete;

    auto operator=(Component&& other)
    -> Component& = delete;

    explicit Component(std::string name);

    virtual ~Component() = default;

public:
    void initialization_depends_on(std::shared_ptr<Component> a)
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

    virtual void initialize_component()
    {
    }

    virtual void on_thread_exit()
    {
    }

    virtual void on_thread_enter()
    {
    }

    auto name() const
    -> const std::string&
    {
        return m_name;
    }

    auto is_initialized() const
    -> bool;

    auto is_registered() const
    -> bool
    {
        return m_is_registered;
    }

    void register_as_component()
    {
        m_is_registered = true;
    }

    void unregister()
    {
        m_is_registered = false;
    }

    void initialize();

    auto ready() const
    -> bool;

    void remove_dependencies(const std::set<std::shared_ptr<Component>>& remove_set);

    auto dependencies()
    -> const std::set<std::shared_ptr<Component>>&
    {
        return m_dependencies;
    }

private:
    std::string                          m_name;
    bool                                 m_initialized{false};
    bool                                 m_is_registered{false};
    std::set<std::shared_ptr<Component>> m_dependencies;
};

} // namespace erhe::components

#endif
