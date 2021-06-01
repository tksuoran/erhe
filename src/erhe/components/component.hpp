#pragma once

#include "erhe/components/components.hpp"
#include "erhe/toolkit/verify.hpp"
#include <set>

namespace erhe::components
{

class Components;

class Component
{
public:
    enum class Component_state : unsigned int
    {
        Constructed,
        Connected,
        Initializing,
        Ready
    };

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

    template<typename T>
    auto get() const -> std::shared_ptr<T>
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
        VERIFY(component);
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

    auto get_state() const
    -> Component_state;

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

    auto is_ready() const
    -> bool;

    void remove_dependency(const std::shared_ptr<Component>& component);

    void initialization_depends_on(const std::shared_ptr<Component>& dependency);

    auto dependencies()
    -> const std::set<std::shared_ptr<Component>>&
    {
        return m_dependencies;
    }

    void set_connected();
    void set_initializing();
    void set_ready();

private:
    const char*                          m_name      {nullptr};
    Component_state                      m_state     {Component_state::Constructed};
    Components*                          m_components{nullptr};
    std::set<std::shared_ptr<Component>> m_dependencies;
};

} // namespace erhe::components
