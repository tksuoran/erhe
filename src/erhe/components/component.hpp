#pragma once

#include "erhe/components/components.hpp"
#include <set>

namespace erhe::components
{

class Components;

class Component
{
protected:
    Component(const Component& other) = delete;

    Component(Component&& other) = delete;

    auto operator=(const Component&)
    -> Component& = delete;

    auto operator=(Component&& other)
    -> Component& = delete;

    explicit Component(const char* name);

    virtual ~Component() = default;

public:
    virtual void connect() {};

    void initialization_depends_on(const std::shared_ptr<Component>& a);

    template<typename T>
    auto get() -> std::shared_ptr<T>
    {
        if (m_components == nullptr)
        {
            return nullptr;
        }
        for (const auto& component : m_components->components)
        {
            auto typed_component = dynamic_pointer_cast<T>(component);
            if (typed_component)
            {
                return typed_component;
            }
        }
        return {};
    }

    template<typename T>
    auto require() -> std::shared_ptr<T>
    {
        auto component = get<T>();
        initialization_depends_on(component);
        return component;
    }

    virtual void initialize_component() {}

    virtual void on_thread_exit() {}

    virtual void on_thread_enter() {}

    auto name() const
    -> const char*
    {
        return m_name;
    }

    auto is_initialized() const
    -> bool;

    auto is_registered() const
    -> bool
    {
        return m_components != nullptr;
    }

    void register_as_component(erhe::components::Components* components)
    {
        m_components = components;
    }

    void unregister()
    {
        m_components = nullptr;
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
    const char*                          m_name{nullptr};
    bool                                 m_initialized{false};
    std::set<std::shared_ptr<Component>> m_dependencies;
    Components*                          m_components{nullptr};
};

} // namespace erhe::components
