#pragma once

#include "erhe/components/components.hpp"
#include "erhe/toolkit/verify.hpp"

#include <memory>
#include <set>
#include <string_view>

namespace erhe::components
{

class Components;

class Time_context
{
public:
    double   dt;
    double   time;
    uint64_t frame_number;
};

class IUpdate_fixed_step
{
public:
    virtual void update_fixed_step(const Time_context&) = 0;
};

class IUpdate_once_per_frame
{
public:
    virtual void update_once_per_frame(const Time_context&) = 0;
};

// Workaround for https://stackoverflow.com/questions/9838862/why-argument-dependent-lookup-doesnt-work-with-function-template-dynamic-pointe
template<int>
void dynamic_pointer_cast();

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
    Component     (const Component&) = delete;
    Component     (Component&&)      = delete;
    void operator=(const Component&) = delete;
    void operator=(Component&&)      = delete;

    explicit Component(const std::string_view name);

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
            const auto typed_component = dynamic_pointer_cast<T>(component);
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
        const auto component = get<T>();
        VERIFY(component);
        initialization_depends_on(component);
        return component;
    }

    virtual void initialize_component() {}

    virtual void on_thread_exit() {}

    virtual void on_thread_enter() {}

    virtual auto initialization_requires_main_thread() const -> bool { return false; }

    auto name() const -> std::string_view
    {
        return m_name;
    }

    auto get_state() const -> Component_state;

    auto is_registered() const -> bool
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

    auto is_ready_to_initialize(const bool in_worker_thread) const -> bool;

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

protected:
    Components*                          m_components{nullptr};

private:
    std::string_view                     m_name;
    Component_state                      m_state      {Component_state::Constructed};
    std::set<std::shared_ptr<Component>> m_dependencies;
};

} // namespace erhe::components
